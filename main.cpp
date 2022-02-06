#include <stdlib.h>

#include <poplar/Engine.hpp>
#include <poplar/IPUModel.hpp>

#include "JPGReader.hpp"

int main(int argc, char** argv) {
  if (argc != 2) {
    printf("USAGE: %s <jpgfile>\n", argv[0]);
    return EXIT_FAILURE;
  }

  poplar::IPUModel ipuModel;
  poplar::Device ipuDevice = ipuModel.createDevice();
  poplar::Target ipuTarget = ipuDevice.getTarget();

  const char* filename = argv[1];
  auto reader = std::make_unique<JPGReader>(ipuDevice, true);
  reader->read(filename);
  reader->decode();
  reader->write(reader->isGreyScale() ? "outfile.pgm" : "outfile.ppm");

  if (TIMINGSTATS) {
    // Warmup
    for (auto i = 0; i < 5; ++i) {
      reader->read(filename);
      reader->decode();
    }
    reader->timings.clear();
    for (auto i = 0; i < 50; ++i) {
      reader->read(filename);
      reader->decode();
    }
    reader->printTimingStats();
  }

  return EXIT_SUCCESS;
}