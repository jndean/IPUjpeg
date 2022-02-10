
#include "codelets.hpp"

#include <poplar/Vertex.hpp>

void iDCT_row(short* D);
void iDCT_col(short* D, int stride);
void iDCT(short* data, int pixels_per_tile, int stride);

inline unsigned char clip(const int x) { return (x < 0) ? 0 : ((x > 0xFF) ? 0xFF : (unsigned char)x); }


template <bool do_iDCT, typename T_coeff> 
class postProcessColour : public poplar::Vertex {
 public:
  poplar::Input<poplar::Vector<int>> params;

  poplar::InOut<poplar::Vector<T_coeff>> Y;
  poplar::InOut<poplar::Vector<T_coeff>> CB;
  poplar::InOut<poplar::Vector<T_coeff>> CR;

  poplar::Output<poplar::Vector<unsigned char>> RGB;

  bool compute() {
    int CB_downshift_y = params[param_CB_downshift_y];
    int CB_downshift_x = params[param_CB_downshift_x];
    int CR_downshift_y = params[param_CR_downshift_y];
    int CR_downshift_x = params[param_CR_downshift_x];
    int Y_stride = params[param_MCU_width];
    int CB_stride = params[param_MCU_width] >> CB_downshift_x;
    int CR_stride = params[param_MCU_width] >> CR_downshift_x;
    int MCU_height = params[param_MCU_height];
    int Y_MCU_pixels = MCU_height * Y_stride;
    int CB_MCU_pixels = (MCU_height >> CB_downshift_y) * CB_stride;
    int CR_MCU_pixels = (MCU_height >> CR_downshift_y) * CR_stride;
    int MCUs_per_tile = params[param_MCUs_per_tile];
    int num_channels = params[param_num_channels];

    // Do iDCT //
    if (do_iDCT) {
      iDCT((short *)&Y[0], MCUs_per_tile * Y_MCU_pixels, Y_stride);
      iDCT((short *)&CB[0], MCUs_per_tile * CB_MCU_pixels, CB_stride);
      iDCT((short *)&CR[0], MCUs_per_tile * CR_MCU_pixels, CR_stride);
    }

    // Do fused upscale and YCbCr->RGB colour transform //
    for (int Y_y = 0; Y_y < MCUs_per_tile * MCU_height; Y_y++) {
      const T_coeff* CB_row = &CB[(Y_y >> CB_downshift_y) * CB_stride];
      const T_coeff* CR_row = &CR[(Y_y >> CR_downshift_y) * CR_stride];

      for (int Y_x = 0; Y_x < Y_stride; ++Y_x) {
        int pixel = Y_y * Y_stride + Y_x;

        int y = Y[pixel] << 8;
        int cb = CB_row[Y_x >> CB_downshift_x] - 128;
        int cr = CR_row[Y_x >> CR_downshift_x] - 128;

        unsigned char* out = &RGB[3 * pixel];
        *(out++) = clip((y + 359 * cr + 128) >> 8);            // R
        *(out++) = clip((y - 88 * cb - 183 * cr + 128) >> 8);  // G
        *(out) = clip((y + 454 * cb + 128) >> 8);              // B
      }
    }

    return true;
  }
};

template class postProcessColour<true, short>;
template class postProcessColour<false, unsigned char>;




void iDCT(short* data, int pixels_per_tile, int stride) {
  for (int pos = 0; pos < pixels_per_tile; pos += 8) {
    iDCT_row(&data[pos]);
  }
  for (int pos = 0; pos < pixels_per_tile; pos += 8 * stride) {
    for (int col = 0; col < stride; ++col) {
      iDCT_col(&data[pos + col], stride);
    }
  }
}


// Precomputed DCT constants //
#define W1 2841
#define W2 2676
#define W3 2408
#define W5 1609
#define W6 1108
#define W7 565

void iDCT_row(short* D) {
  int x0, x1, x2, x3, x4, x5, x6, x7, x8;

  // Block is solid colour //
  if (!((x1 = D[4] << 11) | (x2 = D[6]) | (x3 = D[2]) | (x4 = D[1]) | (x5 = D[7]) | (x6 = D[5]) |
        (x7 = D[3]))) {
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

void iDCT_col(short* D, int stride) {
  int x1 = ((int)D[stride * 4]) << 8;
  int x2 = D[stride * 6];
  int x3 = D[stride * 2];
  int x4 = D[stride * 1];
  int x5 = D[stride * 7];
  int x6 = D[stride * 5];
  int x7 = D[stride * 3];

  // Block is solid colour //
  if (!(x1 | x2 | x3 | x4 | x5 | x6 | x7)) {
    unsigned char x0 = clip(((((int)D[0]) + 32) >> 6) + 128);
    for (int i = 0; i < 8; ++i) {
      D[i * stride] = x0;
    }
    return;
  }

  int x0 = (((int)D[0]) << 8) + 8192;
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

  D[stride * 0] = clip(((x7 + x1) >> 14) + 128);
  D[stride * 1] = clip(((x3 + x2) >> 14) + 128);
  D[stride * 2] = clip(((x0 + x4) >> 14) + 128);
  D[stride * 3] = clip(((x8 + x6) >> 14) + 128);
  D[stride * 4] = clip(((x8 - x6) >> 14) + 128);
  D[stride * 5] = clip(((x0 - x4) >> 14) + 128);
  D[stride * 6] = clip(((x3 - x2) >> 14) + 128);
  D[stride * 7] = clip(((x7 - x1) >> 14) + 128);
}