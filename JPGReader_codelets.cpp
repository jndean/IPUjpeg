
#include <poplar/Vertex.hpp>

#include "JPGReader_params.hpp"

inline unsigned char clip(const int x) { return (x < 0) ? 0 : ((x > 0xFF) ? 0xFF : (unsigned char)x); }

class ColourConversion : public poplar::Vertex {
 public:
  poplar::Input<poplar::Vector<int>> params;

  poplar::Input<poplar::Vector<unsigned char>> Y;
  poplar::Input<poplar::Vector<unsigned char>> CB;
  poplar::Input<poplar::Vector<unsigned char>> CR;

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