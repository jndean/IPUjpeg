TARGET   = main

CFLAGS   = --std=c++14 -Wall -O3 -Wextra
LIBS     = -lpoplar
INCS     = -I/opt/poplar/include

default: main.o JPGReader.o JPGReader_UpsampleColourTransform.o JPGReader_decodescan.o
	g++ ${CFLAGS} $^ ${INCS} ${LIBS} -o ${TARGET}

%.o: %.cpp JPGReader.hpp JPGReader_params.hpp
	g++ ${CFLAGS} -c $< ${INCS} ${LIBS} -o $@

%.gp: %.cpp JPGReader.hpp JPGReader_params.hpp
	popc $< -o $@

clean:
	rm *.o ${TARGET} outfile.ppm outfile.pgm
