#include "JPGReader.hpp"


int main()
{
    JPGReader reader("imgs/small_restart.jpg");
    reader.decode();

    reader.write(reader.isGreyScale() ? "outfile.pgm" : "outfile.ppm");

    return 0;
}