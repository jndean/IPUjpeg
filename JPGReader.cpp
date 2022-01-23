#include "JPGReader.hpp"

#include <string.h>

#include <stdexcept>

JPGReader::JPGReader(poplar::Device &ipuDevice, bool do_iDCT_on_IPU)
    : m_ready_to_decode(false),
      m_do_iDCT_on_IPU(do_iDCT_on_IPU),
      m_ipu_graph(ipuDevice.getTarget()),
      m_num_tiles(ipuDevice.getTarget().getNumTiles()),
      m_max_pixels(m_num_tiles * MAX_PIXELS_PER_TILE),
      m_error(NO_ERROR),
      m_pixels(m_max_pixels * 3),
      m_restart_interval(0),
      m_num_bufbits(0) {
  for (auto &channel : m_channels) {
    channel.pixels.resize(m_max_pixels);
    channel.frequencies.resize(m_max_pixels);
  }
  buildIpuGraph(ipuDevice);
};

void JPGReader::read(const char *filename) {
  if (m_ready_to_decode) flush();

  FILE *f = NULL;
  f = fopen(filename, "r");
  if (NULL == f) goto FAILURE;
  fseek(f, 0, SEEK_END);
  m_size = ftell(f);

  m_buf = std::vector<unsigned char>(m_size);
  fseek(f, 0, SEEK_SET);
  if (fread(m_buf.data(), 1, m_size, f) != m_size) goto FAILURE;
  fclose(f);
  f = NULL;

  // Check Magics //
  if ((m_buf[0] != 0xFF) || (m_buf[1] != 0xD8) || (m_buf[m_size - 2] != 0xFF) || (m_buf[m_size - 1] != 0xD9))
    goto FAILURE;

  if (m_size < 6) goto FAILURE;
  m_end = m_buf.data() + m_size;
  m_pos = m_buf.data() + 2;

  m_ready_to_decode = true;
  return;

FAILURE:
  if (NULL != f) fclose(f);
  throw std::runtime_error("Failed to read file");
}

void JPGReader::flush() {
  m_ready_to_decode = false;
  m_error = NO_ERROR;
  m_restart_interval = 0;
  m_num_bufbits = 0;
  m_buf.clear();
}

JPGReader::~JPGReader() { flush(); }

int JPGReader::decode() {
  if (!m_ready_to_decode) throw std::runtime_error(".read() not called before .decode()");
  m_ready_to_decode = false;

  // Main format block parsing loop //
  while (!m_error) {
    if (m_pos > m_end - 2) {
      m_error = SYNTAX_ERROR;
      break;
    }
    if (m_pos[0] != 0xFF) {
      m_error = SYNTAX_ERROR;
      break;
    }
    m_pos += 2;
    // printf("m_pos=%d, val = 0x(FF)%x\n", m_pos - m_buf.data(), m_pos[-1]);
    switch (m_pos[-1]) {
      case 0xC0:
        decodeSOF();
        break;
      case 0xC4:
        decodeDHT();
        break;
      case 0xDB:
        decodeDQT();
        break;
      case 0xDD:
        decodeDRI();
        break;
      case 0xDA:
        decodeScanCPU();
        break;
      case 0xFE:
        skipBlock();
        break;
      case 0xD9:
        break;
      default:
        if ((m_pos[-1] & 0xF0) == 0xE0) {
          skipBlock();
        } else {
          m_error = SYNTAX_ERROR;
        }
    }

    // Finished //
    if (m_pos[-1] == 0xD9 && m_pos == m_end) {
      upsampleAndColourTransformIPU();
      break;
    }
  }

  if (m_error) {
    fprintf(stderr, "Decode failed with error code %d\n", m_error);
    return m_error;
  }

  return NO_ERROR;
}

void JPGReader::write(const char *filename) {
  FILE *f = fopen(filename, "wb");
  if (!f) {
    printf("Couldn't open output file %s\n", filename);
    return;
  }
  fprintf(f, "P%d\n%d %d\n255\n", (m_num_channels > 1) ? 6 : 5, m_width, m_height);

  // Linearise pixels //
  std::vector<unsigned char> outbuf(m_width * m_height * 3);
  unsigned char *inbuf = (m_num_channels == 1) ? m_channels[0].pixels.data() : m_pixels.data();
  int out_MCU_x = 0, out_MCU_y = 0;
  for (int tile = 0; tile < m_num_active_tiles; tile++) {
    for (int in_MCU = 0; in_MCU < m_MCUs_per_tile; ++in_MCU) {
      if (out_MCU_y >= m_num_MCUs_y) break;
      int in_start = (tile * MAX_PIXELS_PER_TILE) + (in_MCU * m_MCU_size_x * m_MCU_size_y);
      int out_start = out_MCU_y * m_MCU_size_y * m_width + out_MCU_x * m_MCU_size_x;
      int out_width = std::min(m_MCU_size_x, m_width - (out_MCU_x * m_MCU_size_x));
      int out_height = std::min(m_MCU_size_y, m_height - (out_MCU_y * m_MCU_size_y));

      for (int y = 0; y < out_height; ++y) {
        for (int x = 0; x < out_width; ++x) {
          int in_pixel = in_start + y * m_MCU_size_x + x;
          int out_pixel = out_start + y * m_width + x;
          for (int c = 0; c < m_num_channels; ++c) {
            outbuf[out_pixel * m_num_channels + c] = inbuf[in_pixel * m_num_channels + c];
          }
        }
      }

      if (++out_MCU_x == m_num_MCUs_x) {
        out_MCU_y += 1;
        out_MCU_x = 0;
      }
    }
  }

  fwrite(outbuf.data(), sizeof(unsigned char), m_width * m_height * m_num_channels, f);
  fclose(f);
}

unsigned short JPGReader::read16(const unsigned char *pos) { return (pos[0] << 8) | pos[1]; }

void JPGReader::skipBlock() {
  unsigned short block_len = read16(m_pos);
  m_pos += block_len;
}

void JPGReader::decodeSOF() {
  unsigned char *block = m_pos;
  unsigned int block_len = read16(block);
  if (block_len < 9 || block + block_len >= m_end) THROW(SYNTAX_ERROR);
  if (block[2] != 8) THROW(UNSUPPORTED_ERROR);

  // Read image info //
  m_height = read16(&block[3]);
  m_width = read16(&block[5]);
  if (!m_width || !m_height) THROW(SYNTAX_ERROR);
  m_num_channels = block[7];
  if (m_num_channels != 1 && m_num_channels != 3) THROW(UNSUPPORTED_ERROR);

  // Read channel info //
  if (block_len < 8u + (m_num_channels * 3u)) THROW(SYNTAX_ERROR);
  block += 8;
  int i, samples_x_max = 0, samples_y_max = 0;
  ColourChannel *chan = m_channels;
  for (i = 0; i < m_num_channels; i++, chan++, block += 3) {
    chan->id = block[0];
    chan->samples_x = block[1] >> 4;
    chan->samples_y = block[1] & 0xF;
    chan->dq_id = block[2];

    if (!chan->samples_x || !chan->samples_y || chan->dq_id > 3) {
      THROW(SYNTAX_ERROR);
    }
    if ((chan->samples_x & (chan->samples_x - 1)) || (chan->samples_y & (chan->samples_y - 1))) {
      THROW(UNSUPPORTED_ERROR);  // require power of two
    }
    samples_x_max = std::max(samples_x_max, chan->samples_x);
    samples_y_max = std::max(samples_y_max, chan->samples_y);
  }

  if (m_num_channels == 1) {
    m_channels[0].samples_x = samples_x_max = 1;
    m_channels[0].samples_y = samples_y_max = 1;
  }

  // Compute dimensions //
  m_MCU_size_x = samples_x_max * 8;
  m_MCU_size_y = samples_y_max * 8;
  m_num_MCUs_x = (m_width + m_MCU_size_x - 1) / m_MCU_size_x;
  m_num_MCUs_y = (m_height + m_MCU_size_y - 1) / m_MCU_size_y;
  m_MCUs_per_tile = (m_num_MCUs_x * m_num_MCUs_y + m_num_tiles - 1) / m_num_tiles;
  m_num_active_tiles = (m_num_MCUs_x * m_num_MCUs_y + m_MCUs_per_tile - 1) / m_MCUs_per_tile;

  if (m_MCU_size_x * m_MCU_size_y * m_MCUs_per_tile > (int)MAX_PIXELS_PER_TILE) {
    throw std::runtime_error(
        "Image too big. Increase JPGReader::MAX_PIXELS_PER_TILE. "
        "In the future trigger extra downsampling here instead of erroring.");
  }

  for (i = 0, chan = m_channels; i < m_num_channels; i++, chan++) {
    chan->width = (m_width * chan->samples_x + samples_x_max - 1) / samples_x_max;
    chan->height = (m_height * chan->samples_y + samples_y_max - 1) / samples_y_max;
    chan->tile_stride = chan->samples_x * 8;
    chan->pixels_per_MCU = chan->samples_x * 8 * chan->samples_y * 8;
    chan->downshift_x = __builtin_ctz(samples_x_max / chan->samples_x);
    chan->downshift_y = __builtin_ctz(samples_y_max / chan->samples_y);

    if (((chan->width < 3) && (chan->samples_x != samples_x_max)) ||
        ((chan->height < 3) && (chan->samples_y != samples_y_max)))
      THROW(UNSUPPORTED_ERROR);
  }

  m_pos += block_len;
}

void JPGReader::decodeDHT() {
  unsigned char *pos = m_pos;
  unsigned int block_len = read16(pos);
  unsigned char *block_end = pos + block_len;
  if (block_end >= m_end) THROW(SYNTAX_ERROR);
  pos += 2;

  while (pos < block_end) {
    unsigned char val = pos[0];
    if (val & 0xEC) THROW(SYNTAX_ERROR);
    if (val & 0x02) THROW(UNSUPPORTED_ERROR);
    unsigned char table_id = (val | (val >> 3)) & 3;  // AC and DC
    DhtVlc *vlc = &m_vlc_tables[table_id][0];

    unsigned char *tuple = pos + 17;
    int remain = 65536, spread = 65536;
    for (int code_len = 1; code_len <= 16; code_len++) {
      spread >>= 1;
      int count = pos[code_len];
      if (!count) continue;
      if (tuple + count > block_end) THROW(SYNTAX_ERROR);

      remain -= count << (16 - code_len);
      if (remain < 0) THROW(SYNTAX_ERROR);
      for (int i = 0; i < count; i++, tuple++) {
        for (int j = spread; j; j--, vlc++) {
          vlc->num_bits = (unsigned char)code_len;
          vlc->tuple = *tuple;
        }
      }
    }
    while (remain--) {
      vlc->num_bits = 0;
      vlc++;
    }
    pos = tuple;
  }

  if (pos != block_end) THROW(SYNTAX_ERROR);
  m_pos = block_end;
}

void JPGReader::decodeDRI() {
  unsigned int block_len = read16(m_pos);
  unsigned char *block_end = m_pos + block_len;
  if ((block_len < 2) || (block_end >= m_end)) THROW(SYNTAX_ERROR);
  m_restart_interval = read16(m_pos + 2);
  m_pos = block_end;
}

void JPGReader::decodeDQT() {
  unsigned int block_len = read16(m_pos);
  unsigned char *block_end = m_pos + block_len;
  if (block_end >= m_end) THROW(SYNTAX_ERROR);
  unsigned char *pos = m_pos + 2;

  while (pos + 65 <= block_end) {
    unsigned char table_id = pos[0];
    if (table_id & 0xFC) THROW(SYNTAX_ERROR);
    unsigned char *table = &m_dq_tables[table_id][0];
    memcpy(table, pos + 1, 64);
    pos += 65;
  }
  if (pos != block_end) THROW(SYNTAX_ERROR);
  m_pos = block_end;
}

bool JPGReader::isGreyScale() { return m_num_channels == 1; }