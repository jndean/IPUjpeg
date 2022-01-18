TARGET   = main

CFLAGS   = --std=c++14 -Wall -O3 -Wextra

default: main.o CPUReader.o CPUReader_UpsampleColourTransform.o CPUReader_decodescan.o
	g++ ${CFLAGS} $^ -o ${TARGET}

%.o: %.cpp CPUReader.hpp
	g++ ${CFLAGS} -c $< -o $@

clean:
	rm *.o outfile.ppm outfile.pgm ${TARGET}