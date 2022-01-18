#include <stdio.h>
#include <stdlib.h>
#include <memory>

#include "CPUReader.hpp"


int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "USAGE: %s filename.jpg ...\n", argv[0]);
    return EXIT_FAILURE;
  }

  auto reader = std::make_unique<CPUReader>(argv[1]);
  reader->decode();
  const char* outname = reader->isGreyScale() ? "outfile.pgm" : "outfile.ppm";
  reader->write(outname);

  return EXIT_SUCCESS;
}
