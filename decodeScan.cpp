#include <stdio.h>
#include <string.h>

#include <stdexcept>

#include "JPGReader.hpp"


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
    // int value = readNextDhtCode(m_dht_trees[channel->ac_id], &code);
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

int JPGReader::readNextDhtCode(DhtNode *tree, unsigned char *code) {
  int bits = showBits(16);

  unsigned current_node = 0;
  int bits_used = 0;
  while (bits_used < 16) {
    int bit = (bits >> (15 - bits_used)) & 1u;
    current_node = tree[current_node].children[bit];
    bits_used += 1;

    if (tree[current_node].children[0] == 0) break;
  }
  unsigned char RLE_tuple = tree[current_node].tuple;
  m_num_bufbits -= bits_used;
  if (code) *code = RLE_tuple;

  unsigned char num_bits = RLE_tuple & 0x0F;
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

