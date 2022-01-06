TARGET   = main

CFLAGS   = --std=c++11 -Wall -O3
LIBS     = -lpoplar
INCS     = -I/opt/poplar/include

default: main.o JPGReader.o JPGReader_UpsampleColourTransform.o JPGReader_decodescan.o
	g++ ${CFLAGS} $^ ${INCS} ${LIBS} -o ${TARGET}

%.o: %.cpp JPGReader.hpp
	g++ ${CFLAGS} -c $< ${INCS} ${LIBS} -o $@

clean:
	rm *.o ${TARGET} outfile.ppm outfile.pgm
