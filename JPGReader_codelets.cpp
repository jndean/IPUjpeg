
#include <poplar/Vertex.hpp>

#include "JPGReader_params.hpp"

inline unsigned char clip(const int x) { return (x < 0) ? 0 : ((x > 0xFF) ? 0xFF : (unsigned char)x); }

class Postprocess : public poplar::Vertex {
 public:
  poplar::Input<poplar::Vector<int>> params;

  poplar::InOut<poplar::Vector<unsigned char>> Y;
  poplar::InOut<poplar::Vector<unsigned char>> CB;
  poplar::InOut<poplar::Vector<unsigned char>> CR;

  poplar::Output<poplar::Vector<unsigned char>> RGB;

  bool compute() {
    int Y_stride = params[param_MCU_width];
    int CB_stride = params[param_MCU_width] >> params[param_CB_downshift_x];
    int CR_stride = params[param_MCU_width] >> params[param_CR_downshift_x];

    for (int Y_y = 0; Y_y < params[param_MCUs_per_tile] * params[param_MCU_height]; Y_y++) {
      unsigned char* CB_row = &CB[(Y_y >> params[param_CB_downshift_y]) * CB_stride];
      unsigned char* CR_row = &CR[(Y_y >> params[param_CR_downshift_y]) * CR_stride];

      for (int Y_x = 0; Y_x < Y_stride; ++Y_x) {
        int pixel = Y_y * Y_stride + Y_x;
        
        int y = Y[pixel] << 8;
        int cb = CB_row[Y_x >> params[param_CB_downshift_x]] - 128;
        int cr = CR_row[Y_x >> params[param_CR_downshift_x]] - 128;

        RGB[3 * pixel    ] = clip((y + 359 * cr + 128) >> 8);
        RGB[3 * pixel + 1] = clip((y -  88 * cb - 183 * cr + 128) >> 8);
        RGB[3 * pixel + 2] = clip((y + 454 * cb + 128) >> 8);
      }
    }

    return true;
  }
};

/*
void iDCT_row(int *D) {
  int x0, x1, x2, x3, x4, x5, x6, x7, x8;
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
*/