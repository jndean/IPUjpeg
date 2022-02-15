#include "CPUReader.hpp"

#include <string.h>

#include <chrono>
#include <numeric>
#include <stdexcept>

#define SAFEDELETE(ptr)   \
  do {                    \
    if (nullptr != ptr) { \
      delete ptr;         \
      ptr = nullptr;      \
    }                     \
  } while (0)

CPUReader::CPUReader()
    : m_ready_to_decode(false),
      m_buf(nullptr),
      m_error(NO_ERROR),
      m_pixels(nullptr),
      m_restart_interval(0),
      m_num_bufbits(0) {
  for (auto &channel : m_channels) {
    channel.pixels = nullptr;
  }
}

void CPUReader::read(const char *filename) {
  if (m_ready_to_decode) flush();

  FILE *f = NULL;
  f = fopen(filename, "r");
  if (NULL == f) goto FAILURE;
  fseek(f, 0, SEEK_END);
  m_size = ftell(f);

  m_buf = new unsigned char[m_size];
  if (NULL == m_buf) goto FAILURE;
  fseek(f, 0, SEEK_SET);
  if (fread(m_buf, 1, m_size, f) != m_size) goto FAILURE;
  fclose(f);
  f = NULL;

  // Check Magics //
  if ((m_buf[0] != 0xFF) || (m_buf[1] != 0xD8) || (m_buf[m_size - 2] != 0xFF) || (m_buf[m_size - 1] != 0xD9))
    goto FAILURE;

  if (m_size < 6) goto FAILURE;
  m_end = m_buf + m_size;
  m_pos = m_buf + 2;

  m_ready_to_decode = true;
  return;

FAILURE:
  if (NULL != f) fclose(f);
  SAFEDELETE(m_buf);
  throw std::runtime_error("Failed to create jpg reader");
}

void CPUReader::flush() {
  SAFEDELETE(m_buf);
  for (auto &channel : m_channels) SAFEDELETE(channel.pixels);
  SAFEDELETE(m_pixels);
  m_ready_to_decode = false;
}

CPUReader::~CPUReader() { flush(); }

int CPUReader::decode() {
  if (!m_ready_to_decode) {
    throw std::runtime_error(".read() not called before .decode()");
  }
  // Reset state in case we've already called decode since calling read (e.g. profiling)
  for (auto &channel : m_channels) {
    SAFEDELETE(channel.pixels);
  }
  SAFEDELETE(m_pixels);
  m_error = NO_ERROR;
  m_restart_interval = 0;
  m_num_bufbits = 0;

  auto start_time = std::chrono::high_resolution_clock::now();

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
    switch (m_pos[-1]) {
      case 0xC0:
        callAndTime(&CPUReader::decodeSOF, "decodeSOF");
        break;
      case 0xC4:
        callAndTime(&CPUReader::decodeDHT, "decodeDHT");
        break;
      case 0xDB:
        callAndTime(&CPUReader::decodeDQT, "decodeDQT");
        break;
      case 0xDD:
        callAndTime(&CPUReader::decodeDRI, "decodeDRI");
        break;
      case 0xDA:
        callAndTime(&CPUReader::decodeScanCPU, "decodeScanCPU");
        break;
      case 0xFE:
        callAndTime(&CPUReader::skipBlock, "skipBlock");
        break;
      case 0xD9:
        break;
      default:
        if ((m_pos[-1] & 0xF0) == 0xE0)
          callAndTime(&CPUReader::skipBlock, "skipBlock");
        else
          m_error = SYNTAX_ERROR;
    }

    // Finished //
    if (m_pos[-1] == 0xD9 && m_pos == m_end) {
      callAndTime(&CPUReader::upsampleAndColourTransform, "upsampleAndColourTransform");
      break;
    }
  }

  if (m_error) {
    fprintf(stderr, "Decode failed with error code %d\n", m_error);
    return m_error;
  }

  if (TIMINGSTATS) {
    auto elapsed = std::chrono::high_resolution_clock::now() - start_time;
    auto dt = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    timings["decode"].push_back(dt);
  }

  return NO_ERROR;
}

void CPUReader::write(const char *filename) {
  FILE *f = fopen(filename, "wb");
  if (!f) {
    printf("Couldn't open output file %s\n", filename);
    return;
  }
  fprintf(f, "P%d\n%d %d\n255\n", (m_num_channels > 1) ? 6 : 5, m_width, m_height);
  fwrite((m_num_channels == 1) ? m_channels[0].pixels : m_pixels, 1, m_width * m_height * m_num_channels, f);
  fclose(f);
}

unsigned short CPUReader::read16(const unsigned char *pos) { return (pos[0] << 8) | pos[1]; }

void CPUReader::skipBlock() { m_pos += read16(m_pos); }

void CPUReader::decodeSOF() {
  unsigned char *block = m_pos;
  unsigned int block_len = read16(block);
  if (block_len < 9 || block + block_len >= m_end) THROW(SYNTAX_ERROR);
  if (block[2] != 8) THROW(UNSUPPORTED_ERROR);

  // Read image info //
  m_height = read16(block + 3);
  m_width = read16(block + 5);
  if (!m_width || !m_height) THROW(SYNTAX_ERROR);
  m_num_channels = block[7];
  if (m_num_channels != 1 && m_num_channels != 3) THROW(UNSUPPORTED_ERROR);

  // Read channel info //
  if (block_len < 8 + (m_num_channels * 3)) THROW(SYNTAX_ERROR);
  block += 8;
  int i, samples_x_max = 0, samples_y_max = 0;
  ColourChannel *chan = m_channels;
  for (i = 0; i < m_num_channels; i++, chan++, block += 3) {
    chan->id = block[0];
    chan->samples_x = block[1] >> 4;
    chan->samples_y = block[1] & 0xF;
    chan->dq_id = block[2];

    if (!chan->samples_x || !chan->samples_y || chan->dq_id > 3) THROW(SYNTAX_ERROR);
    if ((chan->samples_x & (chan->samples_x - 1)) || (chan->samples_y & (chan->samples_y - 1)))
      THROW(UNSUPPORTED_ERROR);  // require power of two
    if (chan->samples_x > samples_x_max) samples_x_max = chan->samples_x;
    if (chan->samples_y > samples_y_max) samples_y_max = chan->samples_y;
  }

  if (m_num_channels == 1) {
    m_channels[0].samples_x = samples_x_max = 1;
    m_channels[0].samples_y = samples_y_max = 1;
  }

  // Compute dimensions in blocks and allocate output space //
  m_MCU_size_x = samples_x_max << 3;
  m_MCU_size_y = samples_y_max << 3;
  m_num_MCUs_x = (m_width + m_MCU_size_x - 1) / m_MCU_size_x;
  m_num_MCUs_y = (m_height + m_MCU_size_y - 1) / m_MCU_size_y;

  for (i = 0, chan = m_channels; i < m_num_channels; i++, chan++) {
    chan->width = (m_width * chan->samples_x + samples_x_max - 1) / samples_x_max;
    chan->height = (m_height * chan->samples_y + samples_y_max - 1) / samples_y_max;
    chan->stride = m_num_MCUs_x * (chan->samples_x << 3);

    if (((chan->width < 3) && (chan->samples_x != samples_x_max)) ||
        ((chan->height < 3) && (chan->samples_y != samples_y_max)))
      THROW(UNSUPPORTED_ERROR);

    chan->pixels = new unsigned char[chan->stride * m_num_MCUs_y * (chan->samples_y << 3)];
    if (!chan->pixels) THROW(OOM_ERROR);
  }
  if (m_num_channels == 3) {
    m_pixels = new unsigned char[m_width * m_height * 3];
    if (!m_pixels) THROW(OOM_ERROR);
  }

  m_pos += block_len;
}

void CPUReader::decodeDHT() {
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

void CPUReader::decodeDRI() {
  unsigned int block_len = read16(m_pos);
  unsigned char *block_end = m_pos + block_len;
  if ((block_len < 2) || (block_end >= m_end)) THROW(SYNTAX_ERROR);
  m_restart_interval = read16(m_pos + 2);
  m_pos = block_end;
}

void CPUReader::decodeDQT() {
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

bool CPUReader::isGreyScale() { return m_num_channels == 1; }

// ----------------- Utilities for timing (profiling) ------------------------ //

void CPUReader::callAndTime(void (CPUReader::*method)(), const std::string name) {
  if (TIMINGSTATS) {
    auto t = std::chrono::high_resolution_clock::now();
    (this->*method)();
    auto elapsed = std::chrono::high_resolution_clock::now() - t;
    long long microseconds = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    timings[name].push_back(microseconds);
  } else {
    (this->*method)();
  }
}

void CPUReader::printTimingStats() {
  printf(
      "+-------------------------------+-----------+\n"
      "|                        Method | Time (ms) |\n"
      "+-------------------------------+-----------+\n");
  for (const auto &item : timings) {
    double total_microseconds = std::accumulate(item.second.begin(), item.second.end(), 0.);
    double avg_milliseconds = total_microseconds / (1000. * item.second.size());
    if (avg_milliseconds > 0.005) {
      printf("|%30s | % 9.3f |\n", item.first.c_str(), avg_milliseconds);
    } else {
      printf("|%30s |         - |\n", item.first.c_str());
    }
  }
  printf("+-------------------------------+-----------+\n");
}