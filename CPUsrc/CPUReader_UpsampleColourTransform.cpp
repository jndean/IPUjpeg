#include <stdio.h>
#include <string.h>

#include "CPUReader.hpp"


inline unsigned char clip(const int x)
{
    return (x < 0) ? 0 : ((x > 0xFF) ? 0xFF : (unsigned char)x);
}

// DCT is done in place (or elsewher if specified) by doing iDCT_row on each
// row of a block, then iDCT column on each column.

// Precomputed DCT constants //
#define W1 2841
#define W2 2676
#define W3 2408
#define W5 1609
#define W6 1108
#define W7 565

void CPUReader::iDCT_row(int *D)
{
    int x0, x1, x2, x3, x4, x5, x6, x7, x8;
    if (!((x1 = D[4] << 11) | (x2 = D[6]) | (x3 = D[2]) | (x4 = D[1]) | (x5 = D[7]) | (x6 = D[5]) | (x7 = D[3])))
    {
        D[0] = D[1] = D[2] = D[3] = D[4] = D[5] = D[6] = D[7] = D[0] << 3;
        return;
    }
    x0 = (D[0] << 11) + 128;
    x8 = W7 * (x4 + x5);
    x4 = x8 + (W1 - W7) * x4;
    x5 = x8 - (W1 + W7) * x5;
    x8 = W3 * (x6 + x7);
    x6 = x8 - (W3 - W5) * x6;
    x7 = x8 - (W3 + W5) * x7;
    x8 = x0 + x1;
    x0 -= x1;
    x1 = W6 * (x3 + x2);
    x2 = x1 - (W2 + W6) * x2;
    x3 = x1 + (W2 - W6) * x3;
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
    D[0] = (x7 + x1) >> 8;
    D[1] = (x3 + x2) >> 8;
    D[2] = (x0 + x4) >> 8;
    D[3] = (x8 + x6) >> 8;
    D[4] = (x8 - x6) >> 8;
    D[5] = (x0 - x4) >> 8;
    D[6] = (x3 - x2) >> 8;
    D[7] = (x7 - x1) >> 8;
}

void CPUReader::iDCT_col(const int *D, unsigned char *out, int stride)
{
    int x0, x1, x2, x3, x4, x5, x6, x7, x8;
    if (!((x1 = D[8 * 4] << 8) | (x2 = D[8 * 6]) | (x3 = D[8 * 2]) | (x4 = D[8 * 1]) | (x5 = D[8 * 7]) | (x6 = D[8 * 5]) | (x7 = D[8 * 3])))
    {
        x1 = clip(((D[0] + 32) >> 6) + 128);
        for (x0 = 8; x0; --x0)
        {
            *out = (unsigned char)x1;
            out += stride;
        }
        return;
    }
    x0 = (D[0] << 8) + 8192;
    x8 = W7 * (x4 + x5) + 4;
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
    *out = clip(((x7 + x1) >> 14) + 128);
    out += stride;
    *out = clip(((x3 + x2) >> 14) + 128);
    out += stride;
    *out = clip(((x0 + x4) >> 14) + 128);
    out += stride;
    *out = clip(((x8 + x6) >> 14) + 128);
    out += stride;
    *out = clip(((x8 - x6) >> 14) + 128);
    out += stride;
    *out = clip(((x0 - x4) >> 14) + 128);
    out += stride;
    *out = clip(((x3 - x2) >> 14) + 128);
    out += stride;
    *out = clip(((x7 - x1) >> 14) + 128);
}

void CPUReader::upsampleChannel(ColourChannel *channel)
{
    int x, y, xshift = 0, yshift = 0;
    unsigned char *out, *lout;
    while (channel->width < m_width)
    {
        channel->width <<= 1;
        ++xshift;
    }
    while (channel->height < m_height)
    {
        channel->height <<= 1;
        ++yshift;
    }
    out = new unsigned char[channel->width * channel->height];
    if (!out)
        THROW(OOM_ERROR);

    for (y = 0, lout = out; y < channel->height; ++y, lout += channel->width)
    {
        unsigned char *lin = &channel->pixels[(y >> yshift) * channel->stride];
        for (x = 0; x < channel->width; ++x)
            lout[x] = lin[x >> xshift];
    }

    channel->stride = channel->width;
    delete channel->pixels;
    channel->pixels = out;
}


void CPUReader::upsampleAndColourTransform()
{
    int i;
    ColourChannel *channel;
    for (i = 0, channel = &m_channels[0]; i < m_num_channels; ++i, ++channel)
    {
        if ((channel->width < m_width) || (channel->height < m_height))
            upsampleChannel(channel);
        if ((channel->width < m_width) || (channel->height < m_height))
        {
            fprintf(stderr, "Logical error?\n");
            THROW(SYNTAX_ERROR);
        }
    }
    if (m_num_channels == 3)
    {
        // convert to RGB //
        unsigned char *prgb = m_pixels;
        const unsigned char *py = m_channels[0].pixels;
        const unsigned char *pcb = m_channels[1].pixels;
        const unsigned char *pcr = m_channels[2].pixels;
        for (int yy = m_height; yy; --yy)
        {
            for (int x = 0; x < m_width; ++x)
            {
                register int y = py[x] << 8;
                register int cb = pcb[x] - 128;
                register int cr = pcr[x] - 128;
                *prgb++ = clip((y + 359 * cr + 128) >> 8);
                *prgb++ = clip((y - 88 * cb - 183 * cr + 128) >> 8);
                *prgb++ = clip((y + 454 * cb + 128) >> 8);
            }
            py += m_channels[0].stride;
            pcb += m_channels[1].stride;
            pcr += m_channels[2].stride;
        }
    }
    else if (m_channels[0].width != m_channels[0].stride)
    {
        // grayscale -> only remove stride
        ColourChannel *channel = &m_channels[0];
        unsigned char *pin = &channel->pixels[channel->stride];
        unsigned char *pout = &channel->pixels[channel->width];
        for (int y = channel->height - 1; y; --y)
        {
            memcpy(pout, pin, channel->width);
            pin += channel->stride;
            pout += channel->width;
        }
        channel->stride = channel->width;
    }
}
