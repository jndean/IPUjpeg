#include <stdio.h>
#include <string.h>

#include <stdexcept>

#include "JPGReader.hpp"


inline unsigned char tmpclip(const int x) { return (x < 0) ? 0 : ((x > 0xFF) ? 0xFF : (unsigned char)x); }
#define W1 2841
#define W2 2676
#define W3 2408
#define W5 1609
#define W6 1108
#define W7 565
void tmp_iDCT_col(const short *D, short *out, int stride) {
  int x1 = ((int) D[stride * 4]) << 8;
  int x2 = D[stride * 6];
  int x3 = D[stride * 2];
  int x4 = D[stride * 1];
  int x5 = D[stride * 7];
  int x6 = D[stride * 5];
  int x7 = D[stride * 3];

  // Block is solid colour //
  if (!(x1 | x2 | x3 | x4 | x5 | x6 | x7)) {
    unsigned char x0 = tmpclip(((((int) D[0]) + 32) >> 6) + 128);
    for (int i = 0; i < 8; ++i) {
      *out = x0;
      out += stride;
    }
    return;
  }

  int x0 = (((int) D[0]) << 8) + 8192;
  int x8 = W7 * (x4 + x5) + 4;
  x4 = (x8 + (W1 - W7) * x4) >> 3;
  x5 = (x8 - (W1 + W7) * x5) >> 3;
  x8 = W3 * (x6 + x7) + 4;
  x6 = (x8 - (W3 - W5) * x6) >> 3;
  x7 = (x8 - (W3 + W5) * x7) >> 3;
  x8 = x0 + x1;
  x0 -= x1;
  x1 = W6 * (x3 + x2) + 4;
  x2 = (x1 - (W2 + W6) * x2) >> 3;
  x3 = (x1 + (W2 - W6) * x3) >> 3;
  x1 = x4 + x6;
  x4 -= x6;
  x6 = x5 + x7;
  x5 -= x7;
  x7 = x8 + x3;
  x8 -= x3;
  x3 = x0 + x2;
  x0 -= x2;
  x2 = (181 * (x4 + x5) + 128) >> 8;
  x4 = (181 * (x4 - x5) + 128) >> 8;
  *out = tmpclip(((x7 + x1) >> 14) + 128);
  out += stride;
  *out = tmpclip(((x3 + x2) >> 14) + 128);
  out += stride;
  *out = tmpclip(((x0 + x4) >> 14) + 128);
  out += stride;
  *out = tmpclip(((x8 + x6) >> 14) + 128);
  out += stride;
  *out = tmpclip(((x8 - x6) >> 14) + 128);
  out += stride;
  *out = tmpclip(((x0 - x4) >> 14) + 128);
  out += stride;
  *out = tmpclip(((x3 - x2) >> 14) + 128);
  out += stride;
  *out = tmpclip(((x7 - x1) >> 14) + 128);
}


void JPGReader::decodeScanCPU() {
  unsigned char *pos = m_pos;
  unsigned int header_len = read16(pos);
  if (pos + header_len >= m_end) THROW(SYNTAX_ERROR);
  pos += 2;

  if (header_len < (4u + 2u * m_num_channels)) THROW(SYNTAX_ERROR);
  if (*(pos++) != m_num_channels) THROW(UNSUPPORTED_ERROR);
  int i;
  ColourChannel *channel;
  for (i = 0, channel = m_channels; i < m_num_channels; i++, channel++, pos += 2) {
    if (pos[0] != channel->id) THROW(SYNTAX_ERROR);
    if (pos[1] & 0xEE) THROW(SYNTAX_ERROR);
    channel->dc_id = pos[1] >> 4;
    channel->ac_id = (pos[1] & 1) | 2;
  }
  if (pos[0] || (pos[1] != 63) || pos[2]) THROW(UNSUPPORTED_ERROR);
  m_pos += header_len;

  // Iterate over blocks and decode them! //
  int restart_count = m_restart_interval;
  const int total_MCUs = m_num_MCUs_x * m_num_MCUs_y;
  int completed_MCUs = 0;

  for (int tile = 0; tile < m_num_active_tiles; ++tile) {
    for (int MCU = 0; MCU < m_MCUs_per_tile && completed_MCUs < total_MCUs; ++MCU, ++completed_MCUs) {
      for (i = 0, channel = m_channels; i < m_num_channels; ++i, ++channel) {
        int MCU_start = (tile * MAX_PIXELS_PER_TILE) + (MCU * channel->pixels_per_MCU);

        for (int sample_y = 0; sample_y < channel->samples_y; ++sample_y) {
          for (int sample_x = 0; sample_x < channel->samples_x; ++sample_x) {
            int out_pos = MCU_start + (sample_y * channel->tile_stride * 8) + (sample_x * 8);
            decodeBlock(channel, &channel->frequencies[out_pos], &channel->pixels[out_pos]);
            if (m_error) return;
          }
        }
      }

      if (m_restart_interval && !(--restart_count)) {
        // Byte align the read head //
        m_num_bufbits &= 0xF8;
        int marker_bits = getBits(16);
        if ((marker_bits & 0xFF00) != 0xFF00) {
          THROW(SYNTAX_ERROR);
        }
        restart_count = m_restart_interval;
        for (ColourChannel &channel : m_channels) {
          channel.dc_cumulative_val = 0;
        }
      }
    }
  }
}

void JPGReader::decodeBlock(ColourChannel *channel, short *freq_out, unsigned char *pixel_out) {
  int MCU_stride = channel->tile_stride;
  for (int i = 0; i < 8; ++i) {
    memset(&freq_out[i * MCU_stride], 0, 8 * sizeof(short));
  }

  // Read DC value //
  channel->dc_cumulative_val += getVLC(&m_vlc_tables[channel->dc_id][0], NULL);
  freq_out[0] = (channel->dc_cumulative_val) * m_dq_tables[channel->dq_id][0];

  // Read  AC values //
  int pos = 0;
  unsigned char code = 0;
  do {
    int value = getVLC(&m_vlc_tables[channel->ac_id][0], &code);
    if (!code) break;  // EOB marker //
    if (!(code & 0x0F) && (code != 0xF0)) THROW(SYNTAX_ERROR);
    pos += (code >> 4) + 1;
    if (pos >= 64) THROW(SYNTAX_ERROR);
    freq_out[deZigZagY[pos] * MCU_stride + deZigZagX[pos]] = value * m_dq_tables[channel->dq_id][pos];
  } while (pos < 63);

  // Invert the DCT //
  if (!m_do_iDCT_on_IPU) {
    for (int i = 0; i < 8; ++i) iDCT_row(&freq_out[i * MCU_stride]);
    for (int i = 0; i < 8; ++i) iDCT_col(&freq_out[i], &pixel_out[i], MCU_stride);
  } else {
    for (int i = 0; i < 8; ++i) iDCT_row(&freq_out[i * MCU_stride]);
    for (int i = 0; i < 8; ++i) tmp_iDCT_col(&freq_out[i], &freq_out[i], MCU_stride);
  }
}

int JPGReader::getVLC(DhtVlc *vlc_table, unsigned char *code) {
  int symbol = showBits(16);
  DhtVlc vlc = vlc_table[symbol];
  if (!vlc.num_bits) {
    m_error = SYNTAX_ERROR;
    return 0;
  }
  m_num_bufbits -= vlc.num_bits;
  if (code) *code = vlc.tuple;
  unsigned char num_bits = vlc.tuple & 0x0F;
  if (!num_bits) return 0;
  int value = getBits(num_bits);
  if (value < (1 << (num_bits - 1))) value += ((0xffffffff) << num_bits) + 1;
  return value;
}

// This only shows the bits, but doesn't move past them //
int JPGReader::showBits(int num_bits) {
  unsigned char newbyte;
  if (!num_bits) return 0;

  while (m_num_bufbits < num_bits) {
    if (m_pos >= m_end) {
      m_bufbits = (m_bufbits << 8) | 0xFF;
      m_num_bufbits += 8;
      continue;
    }
    newbyte = *m_pos++;
    m_bufbits = (m_bufbits << 8) | newbyte;
    m_num_bufbits += 8;
    if (newbyte != 0xFF) continue;

    if (m_pos >= m_end) goto FAILURE;

    // Handle byte stuffing //
    unsigned char follow_byte = *m_pos++;
    switch (follow_byte) {
      case 0x00:
      case 0xFF:
      case 0xD9:
        break;
      default:
        if ((follow_byte & 0xF8) != 0xD0) {
          goto FAILURE;
        } else {
          m_bufbits = (m_bufbits << 8) | newbyte;
          m_num_bufbits += 8;
        }
    }
  }
  return (m_bufbits >> (m_num_bufbits - num_bits)) & ((1 << num_bits) - 1);

FAILURE:
  m_error = SYNTAX_ERROR;
  return 0;
}

// Show the bits AND move past them //
int JPGReader::getBits(int num_bits) {
  int res = showBits(num_bits);
  m_num_bufbits -= num_bits;
  return res;
}
