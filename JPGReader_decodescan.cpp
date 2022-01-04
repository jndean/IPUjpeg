#include <stdio.h>
#include <string.h>
#include <stdexcept>

#include "JPGReader.hpp"

void JPGReader::decodeScanCPU()
{
  unsigned char *pos = m_pos;
  unsigned int header_len = read16(pos);
  if (pos + header_len >= m_end)
    THROW(SYNTAX_ERROR);
  pos += 2;

  if (header_len < (4 + 2 * m_num_channels))
    THROW(SYNTAX_ERROR);
  if (*(pos++) != m_num_channels)
    THROW(UNSUPPORTED_ERROR);
  int i;
  ColourChannel *channel;
  for (i = 0, channel = m_channels; i < m_num_channels; i++, channel++, pos += 2)
  {
    if (pos[0] != channel->id)
      THROW(SYNTAX_ERROR);
    if (pos[1] & 0xEE)
      THROW(SYNTAX_ERROR);
    channel->dc_id = pos[1] >> 4;
    channel->ac_id = (pos[1] & 1) | 2;
  }
  if (pos[0] || (pos[1] != 63) || pos[2])
    THROW(UNSUPPORTED_ERROR);
  pos = m_pos = m_pos + header_len;

  int restart_interval = m_restart_interval;
  int restart_count = restart_interval;
  int next_restart_index = 0;

  // Loop over all blocks
  for (int block_y = 0; block_y < m_num_blocks_y; block_y++)
  {
    for (int block_x = 0; block_x < m_num_blocks_x; block_x++)
    {

      // Loop over all channels //
      for (i = 0, channel = m_channels; i < m_num_channels; i++, channel++)
      {

        // Loop over samples in block //
        for (int sample_y = 0; sample_y < channel->samples_y; ++sample_y)
        {
          for (int sample_x = 0; sample_x < channel->samples_x; ++sample_x)
          {

            int out_pos = ((block_y * channel->samples_y + sample_y) * channel->stride + block_x * channel->samples_x + sample_x) << 3;
            decodeBlock(channel, &channel->pixels[out_pos]);
            if (m_error)
              return;
          }
        }
      }

      if (restart_interval && !(--restart_count))
      {
        // Byte align //
        m_num_bufbits &= 0xF8;
        int marker_bits = getBits(16);
        if (marker_bits & 0xFF00 != 0xFF00) {
          THROW(SYNTAX_ERROR);
        }
        // jpg encoders don't seem to respect this rule, so we ignore it //
        /*
        if (((i & 0xFFF8) != 0xFFD0) || ((i & 7) != next_restart_index)){
          THROW(SYNTAX_ERROR);
        }
        next_restart_index = (next_restart_index + 1) & 7;
        */
        restart_count = restart_interval;
        for (i = 0; i < 3; i++)
          m_channels[i].dc_cumulative_val = 0;
      }
    }
  }
}

void JPGReader::decodeBlock(ColourChannel *channel, unsigned char *out)
{
  unsigned char code = 0;
  int value, coef = 0;
  int *block = m_block_space;
  memset(block, 0, 64 * sizeof(int));

  // Read DC value //
  channel->dc_cumulative_val += getVLC(&m_vlc_tables[channel->dc_id][0], NULL);
  block[0] = (channel->dc_cumulative_val) * m_dq_tables[channel->dq_id][0];
  // Read  AC values //
  do
  {
    value = getVLC(&m_vlc_tables[channel->ac_id][0], &code);
    if (!code)
      break; // EOB marker //
    if (!(code & 0x0F) && (code != 0xF0))
      THROW(SYNTAX_ERROR);
    coef += (code >> 4) + 1;
    if (coef > 63)
      THROW(SYNTAX_ERROR);
    block[(int)deZigZag[coef]] = value * m_dq_tables[channel->dq_id][coef];
  } while (coef < 63);

  // Invert the DCT //
  for (coef = 0; coef < 64; coef += 8)
    iDCT_row(&block[coef]);
  for (coef = 0; coef < 8; ++coef)
    iDCT_col(&block[coef], &out[coef], channel->stride);
}

int JPGReader::getVLC(DhtVlc *vlc_table, unsigned char *code)
{
  int symbol = showBits(16);
  DhtVlc vlc = vlc_table[symbol];
  if (!vlc.num_bits)
  {
    m_error = SYNTAX_ERROR;
    return 0;
  }
  m_num_bufbits -= vlc.num_bits;
  if (code)
    *code = vlc.tuple;
  unsigned char num_bits = vlc.tuple & 0x0F;
  if (!num_bits)
    return 0;
  int value = getBits(num_bits);
  if (value < (1 << (num_bits - 1)))
    value += ((-1) << num_bits) + 1;
  return value;
}

// This only shows the bits, but doesn't move past them //
int JPGReader::showBits(int num_bits)
{
  unsigned char newbyte;
  if (!num_bits)
    return 0;

  while (m_num_bufbits < num_bits)
  {
    if (m_pos >= m_end)
    {
      m_bufbits = (m_bufbits << 8) | 0xFF;
      m_num_bufbits += 8;
      continue;
    }
    newbyte = *m_pos++;
    m_bufbits = (m_bufbits << 8) | newbyte;
    m_num_bufbits += 8;
    if (newbyte != 0xFF)
      continue;

    if (m_pos >= m_end)
      goto FAILURE;

    // Handle byte stuffing //
    unsigned char follow_byte = *m_pos++;
    switch (follow_byte)
    {
    case 0x00:
    case 0xFF:
    case 0xD9:
      break;
    default:
      if ((follow_byte & 0xF8) != 0xD0)
      {
        goto FAILURE;
      }
      else
      {
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
int JPGReader::getBits(int num_bits)
{
  int res = showBits(num_bits);
  m_num_bufbits -= num_bits;
  return res;
}
