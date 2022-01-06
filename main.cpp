#include "JPGReader.hpp"

#include <stdlib.h>

#include <poplar/IPUModel.hpp>
#include <poplar/Engine.hpp>

int main(int argc, char** argv)
{
    if (argc != 2) {
        printf("USAGE: %s <jpgfile>\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    poplar::IPUModel ipuModel;
    poplar::Device ipuDevice = ipuModel.createDevice();
    poplar::Target ipuTarget = ipuDevice.getTarget();

    JPGReader reader(ipuTarget);
    reader.read(argv[1]);
    reader.decode();

    reader.write(reader.isGreyScale() ? "outfile.pgm" : "outfile.ppm");

    return EXIT_SUCCESS;
}