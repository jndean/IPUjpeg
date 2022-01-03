#include "JPGReader.hpp"


int main()
{
    JPGReader reader("imgs/small420.jpg");
    reader.decode();

    reader.write(reader.isGreyScale() ? "outfile.pgm" : "outfile.ppm");

    return 0;
}