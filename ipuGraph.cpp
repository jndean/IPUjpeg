

#include "JPGReader.hpp"
#include <poputil/VertexTemplates.hpp>

void JPGReader::buildIpuGraph(poplar::Device &ipuDevice) {
  m_ipu_graph.addCodelets("codelets.gp");

  m_IPU_params_tensor = m_ipu_graph.addVariable(poplar::INT, {(ulong)PARAMS_SIZE}, "params_table");
  m_ipu_graph.setTileMapping(m_IPU_params_tensor, 0);
  auto IPU_params_stream = m_ipu_graph.addHostToDeviceFIFO("params-stream", poplar::INT, PARAMS_SIZE);

  // Setup Intermediate and output pixel tensors + streams
  m_out_pixels = m_ipu_graph.addVariable(poplar::UNSIGNED_CHAR, {(ulong)m_max_pixels * 3}, "pixels");
  m_output_pixels_stream =
      m_ipu_graph.addDeviceToHostFIFO("pixels-stream", poplar::UNSIGNED_CHAR, m_max_pixels * 3);
  for (int i = 0; i < 3; ++i) {
    m_channels[i].tensor_name = "channel_0_pixels";
    m_channels[i].stream_name = "channel_0_stream";
    m_channels[i].tensor_name[8] += i;
    m_channels[i].stream_name[8] += i;
    poplar::Type input_type = m_do_iDCT_on_IPU ? poplar::SHORT : poplar::UNSIGNED_CHAR;
    m_channels[i].data_tensor =
        m_ipu_graph.addVariable(input_type, {(ulong)m_max_pixels}, m_channels[i].tensor_name);
    m_channels[i].input_stream =
        m_ipu_graph.addHostToDeviceFIFO(m_channels[i].stream_name, input_type, m_max_pixels);
  }

  // Connect inputs to outputs via compute vertex, and map all over tiles
  poplar::ComputeSet postprocess_op = m_ipu_graph.addComputeSet("postprocess");
  const auto vertexClass = poputil::templateVertex(
    "postProcessColour", 
    m_do_iDCT_on_IPU ? "true" : "false", 
    m_do_iDCT_on_IPU ? "short" : "unsigned char"
  );
  for (unsigned int virtual_tile = 0; virtual_tile < m_num_tiles; ++virtual_tile) {
    int physical_tile = virtual_tile / THREADS_PER_TILE;
    poplar::VertexRef vtx = m_ipu_graph.addVertex(postprocess_op, vertexClass);
    int start = virtual_tile * MAX_PIXELS_PER_TILE;
    int end = (virtual_tile + 1) * MAX_PIXELS_PER_TILE;
    auto Y = m_channels[0].data_tensor.slice(start, end);
    auto CB = m_channels[1].data_tensor.slice(start, end);
    auto CR = m_channels[2].data_tensor.slice(start, end);
    auto RGB = m_out_pixels.slice(start * 3, end * 3);
    m_ipu_graph.connect(vtx["params"], m_IPU_params_tensor);
    m_ipu_graph.connect(vtx["Y"], Y);
    m_ipu_graph.connect(vtx["CB"], CB);
    m_ipu_graph.connect(vtx["CR"], CR);
    m_ipu_graph.connect(vtx["RGB"], RGB);
    m_ipu_graph.setTileMapping(vtx, physical_tile);
    m_ipu_graph.setTileMapping(Y, physical_tile);
    m_ipu_graph.setTileMapping(CB, physical_tile);
    m_ipu_graph.setTileMapping(CR, physical_tile);
    m_ipu_graph.setTileMapping(RGB, physical_tile);

    m_ipu_graph.setPerfEstimate(vtx, MAX_PIXELS_PER_TILE * 1000);
  }

  // Create colour conversion program
  poplar::program::Sequence ipu_postprocess_program;
  ipu_postprocess_program.add(poplar::program::Copy(IPU_params_stream, m_IPU_params_tensor));
  for (auto &channel : m_channels) {
    ipu_postprocess_program.add(poplar::program::Copy(channel.input_stream, channel.data_tensor));
  }
  ipu_postprocess_program.add(poplar::program::Execute(postprocess_op));
  ipu_postprocess_program.add(poplar::program::Copy(m_out_pixels, m_output_pixels_stream));

  // Create poplar engine ("session"?) to execute colour program
  m_ipuEngine = std::make_unique<poplar::Engine>(m_ipu_graph, ipu_postprocess_program);
  m_ipuEngine->connectStream("params-stream", m_IPU_params_table);
  m_ipuEngine->connectStream("pixels-stream", m_pixels.data());
  for (auto &channel : m_channels) {
    void *src = m_do_iDCT_on_IPU ? (void *)channel.frequencies.data() : (void *)channel.pixels.data();
    m_ipuEngine->connectStream(channel.stream_name, src);
  }

  m_ipuEngine->load(ipuDevice);
}