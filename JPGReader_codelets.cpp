
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

  // Compute function
  bool compute() {
    int total_pixels = params[param_MCUs_per_tile] * params[param_Y_MCU_size];
    for (int i = 0; i < total_pixels; i += 1) {
      int y = ((int)Y[i]) << 8;
      int cb = ((int)CB[i]) - 128;
      int cr = ((int)CR[i]) - 128;
      RGB[3 * i] = clip((y + 359 * cr + 128) >> 8);
      RGB[3 * i + 1] = clip((y - 88 * cb - 183 * cr + 128) >> 8);
      RGB[3 * i + 2] = clip((y + 454 * cb + 128) >> 8);
    }
    return true;
  }
};