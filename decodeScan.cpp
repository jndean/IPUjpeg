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

int JPGReader::getBitsAsValue(int num_bits) {
  if (num_bits == 0) return 0;
  int value = getBits(num_bits);
  if (value < (1 << (num_bits - 1))) value += ((0xffffffff) << num_bits) + 1;
  return value;
}

void JPGReader::decodeBlock(ColourChannel *channel, short *freq_out, unsigned char *pixel_out) {
  int MCU_stride = channel->tile_stride;
  for (int i = 0; i < 8; ++i) {
    memset(&freq_out[i * MCU_stride], 0, 8 * sizeof(short));
  }

  // Read DC value //
  unsigned char num_value_bits = decodeRLEtuple(channel->dc_id) & 0x0F;
  channel->dc_cumulative_val += getBitsAsValue(num_value_bits);
  freq_out[0] = (channel->dc_cumulative_val) * m_dq_tables[channel->dq_id][0];

  // Read AC values //
  int pos = 0;
  do {
    // First: read a Huffman encoded RLE tuple //
    unsigned char tuple = decodeRLEtuple(channel->ac_id);
    if (!tuple) break;  // EOB marker
    unsigned char num_value_bits = tuple & 0x0F;
    unsigned char num_zeros = tuple >> 4;
    // If there are no value bits, this must be a run of 16 (i.e. 15+1) zeros
    if (num_value_bits == 0 && (num_zeros != 15)) THROW(SYNTAX_ERROR);
    pos += num_zeros + 1;
    if (pos >= 64) THROW(SYNTAX_ERROR);

    // Second: consume as many bits as specified by the tuple to recover the DCT coefficient value //
    int value = getBitsAsValue(num_value_bits);

    // Third: de-quantise and de-zigzag, placing value in output block //
    freq_out[deZigZagY[pos] * MCU_stride + deZigZagX[pos]] = value * m_dq_tables[channel->dq_id][pos];
  } while (pos < 63);

  // Once the block of coefficients is recovered we can inverse the DCT (or leave it to later) //
  if (!m_do_iDCT_on_IPU) {
    for (int i = 0; i < 8; ++i) iDCT_row(&freq_out[i * MCU_stride]);
    for (int i = 0; i < 8; ++i) iDCT_col(&freq_out[i], &pixel_out[i], MCU_stride);
  }
}

unsigned char JPGReader::decodeRLEtuple(int dht_id) {
  // See if the symbol is short enough to be in the table of precomputed values //
  if (DHT_TABLE_BITS > 0) {
    int symbol = showBits(DHT_TABLE_BITS);
    DhtTableItem vlc = m_dht_tables[dht_id][symbol];
    if (vlc.num_bits > 0) {
      m_num_bufbits -= vlc.num_bits;
      return vlc.tuple;
    }
  }

  // Otherwise do a proper huffman tree lookup //
  int bits = showBits(16);
  DhtNode *tree = &m_dht_trees[dht_id][0];
  unsigned current_node = 0;
  int bits_used = 0;
  while (bits_used < 16) {
    int bit = (bits >> (15 - bits_used)) & 1u;
    current_node = tree[current_node].children[bit];
    bits_used += 1;

    if (tree[current_node].children[0] == 0) break;
  }
  m_num_bufbits -= bits_used;
  return tree[current_node].tuple;
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
