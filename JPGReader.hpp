#pragma once

#include <memory>
#include <poplar/Engine.hpp>
#include <poplar/Graph.hpp>
#include <string>
#include <vector>

#include "JPGReader_params.hpp"


#define NO_ERROR 0
#define SYNTAX_ERROR 1
#define UNSUPPORTED_ERROR 2
#define OOM_ERROR 3

#define THROW(e) \
  do {           \
    m_error = e; \
    return;      \
  } while (0)


typedef struct _DhtVlc {
  unsigned char tuple, num_bits;
} DhtVlc;

typedef struct _ColourChannel {
  int id;
  int dq_id, ac_id, dc_id;
  int width, height;
  int samples_x, samples_y;
  int downshift_x, downshift_y;
  int tile_stride, pixels_per_MCU;
  int dc_cumulative_val;
  std::vector<unsigned char> pixels;
  std::string tensor_name;

  poplar::Tensor ipu_pixels;
  std::vector<poplar::Tensor> ipu_pixel_patches;
  std::string stream_name;
  poplar::DataStream ipu_pixel_stream;
} ColourChannel;


class JPGReader {
 public:
  JPGReader(poplar::Device& ipuDevice);
  ~JPGReader();

  void read(const char* filename);
  int decode();
  void write(const char* filename);
  void flush();

  bool isGreyScale();
  bool readyToDecode();

  static const ulong MAX_PIXELS_PER_TILE = 64 * 64;

 private:
  bool m_ready_to_decode;

  poplar::Graph m_ipu_graph;
  unsigned m_num_tiles;
  int m_max_pixels;
  std::unique_ptr<poplar::Engine> m_ipuEngine;
  poplar::Tensor m_out_pixels;
  std::vector<poplar::Tensor> m_out_pixel_patches;
  poplar::DataStream m_output_pixels_stream;
  int m_IPU_params_table[PARAMS_SIZE];
  poplar::Tensor m_IPU_params_tensor;


  std::vector<unsigned char> m_buf;
  unsigned char *m_pos, *m_end;
  unsigned int m_size;
  unsigned short m_width, m_height;
  unsigned short m_num_MCUs_x, m_num_MCUs_y;
  int m_MCU_size_x, m_MCU_size_y;
  unsigned short m_MCUs_per_tile;
  int m_num_active_tiles;
  unsigned char m_num_channels;
  int m_error;
  ColourChannel m_channels[3];
  std::vector<unsigned char> m_pixels;
  DhtVlc m_vlc_tables[4][65536];
  unsigned char m_dq_tables[4][64];
  int m_restart_interval;
  unsigned int m_bufbits;
  unsigned char m_num_bufbits;
  int m_block_space[64];

  unsigned short read16(const unsigned char* pos);

  void skipBlock();
  void decodeSOF();
  void decodeDHT();
  void decodeDQT();
  void decodeDRI();

  void decodeScanCPU();
  void decodeBlock(ColourChannel* channel, unsigned char* out);
  int getVLC(DhtVlc* vlc_table, unsigned char* code);
  int getBits(int num_bits);
  int showBits(int num_bits);

  void upsampleAndColourTransform();
  void upsampleAndColourTransformIPU();
  void upsampleChannel(ColourChannel* channel);
  void upsampleChannelIPU(ColourChannel *channel);
  void iDCT_row(int* D);
  void iDCT_col(const int* D, unsigned char* out, int stride);
};

static const char deZigZag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                                  12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                                  35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                                  58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
