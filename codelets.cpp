
#include "codelets.hpp"

#include <poplar/Vertex.hpp>

void iDCT_row(short* D);
inline unsigned char clip(const int x) { return (x < 0) ? 0 : ((x > 0xFF) ? 0xFF : (unsigned char)x); }


class UpsampleColourTransform : public poplar::Vertex {
 public:
  poplar::Input<poplar::Vector<int>> params;

  poplar::Input<poplar::Vector<unsigned char>> Y;
  poplar::Input<poplar::Vector<unsigned char>> CB;
  poplar::Input<poplar::Vector<unsigned char>> CR;

  poplar::Output<poplar::Vector<unsigned char>> RGB;

  bool compute() {
    int CB_downshift_y = params[param_CB_downshift_y];
    int CB_downshift_x = params[param_CB_downshift_x];
    int CR_downshift_y = params[param_CR_downshift_y];
    int CR_downshift_x = params[param_CR_downshift_x];
    int Y_stride = params[param_MCU_width];
    int CB_stride = params[param_MCU_width] >> CB_downshift_x;
    int CR_stride = params[param_MCU_width] >> CR_downshift_x;

    for (int Y_y = 0; Y_y < params[param_MCUs_per_tile] * params[param_MCU_height]; Y_y++) {
      unsigned char* CB_row = &CB[(Y_y >> CB_downshift_y) * CB_stride];
      unsigned char* CR_row = &CR[(Y_y >> CR_downshift_y) * CR_stride];

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

class iDCTUpsampleColourTransform : public poplar::Vertex {
 public:
  poplar::Input<poplar::Vector<int>> params;

  poplar::InOut<poplar::Vector<short>> Y;
  poplar::InOut<poplar::Vector<short>> CB;
  poplar::InOut<poplar::Vector<short>> CR;

  poplar::Output<poplar::Vector<unsigned char>> RGB;

  bool compute() {
    int CB_downshift_y = params[param_CB_downshift_y];
    int CB_downshift_x = params[param_CB_downshift_x];
    int CR_downshift_y = params[param_CR_downshift_y];
    int CR_downshift_x = params[param_CR_downshift_x];
    int Y_stride = params[param_MCU_width];
    int CB_stride = params[param_MCU_width] >> CB_downshift_x;
    int CR_stride = params[param_MCU_width] >> CR_downshift_x;
    int Y_MCU_pixels = params[param_MCU_height] * Y_stride;
    int CB_MCU_pixels = (params[param_MCU_height] >> CB_downshift_y) * CB_stride;
    int CR_MCU_pixels = (params[param_MCU_height] >> CR_downshift_y) * CR_stride;

    // Do iDCT //
    /*
    for (int MCU = 0; MCU < params[param_MCUs_per_tile]; ++MCU) {
      short *Y_MCU = &Y[MCU * Y_MCU_pixels];
      short *CB_MCU = &CB[MCU * CB_MCU_pixels];
      short *CR_MCU = &CR[MCU * CR_MCU_pixels];
      for (int row = 0; row < 8; row++){
        iDCT_row(&Y_MCU[row * Y_stride]);
        iDCT_row(&CB_MCU[row * CB_stride]);
        iDCT_row(&CR_MCU[row * CR_stride]);
      }
    }
    */

    // To fused upscale and YCbCr->RGB colour transform //
    for (int Y_y = 0; Y_y < params[param_MCUs_per_tile] * params[param_MCU_height]; Y_y++) {
      short* CB_row = &CB[(Y_y >> CB_downshift_y) * CB_stride];
      short* CR_row = &CR[(Y_y >> CR_downshift_y) * CR_stride];

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
