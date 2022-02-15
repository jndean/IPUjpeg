#include <stdio.h>
#include <stdlib.h>
#include <memory>

#include "CPUReader.hpp"


int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "USAGE: %s filename.jpg ...\n", argv[0]);
    return EXIT_FAILURE;
  }

  const char* filename = argv[1];
  auto reader = std::make_unique<CPUReader>();
  reader->read(filename);
  reader->decode();
  reader->write(reader->isGreyScale() ? "outfile.pgm" : "outfile.ppm");

  if (TIMINGSTATS) {
    // Warmup
    for (auto i = 0; i < 20; ++i) {
      reader->read(filename);
      reader->decode();
    }
    reader->timings.clear();
    for (auto i = 0; i < 100; ++i) {
      reader->read(filename);
      reader->decode();
    }
    reader->printTimingStats();
  }

  return EXIT_SUCCESS;
}
