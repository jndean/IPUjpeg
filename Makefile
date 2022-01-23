CFLAGS   = --std=c++14 -Wall -O3 -Wextra
LIBS     = -lpoplar
INCS     = -I/opt/poplar/include
obj_files = main.o JPGReader.o upsampleColourTransform.o decodeScan.o ipuGraph.o

default: ${obj_files} codelets.gp
	g++ ${CFLAGS} ${obj_files} ${INCS} ${LIBS} -o main

%.o: %.cpp JPGReader.hpp codelets.hpp
	g++ ${CFLAGS} -c $< ${INCS} ${LIBS} -o $@

%.gp: %.cpp %.hpp
	popc $< -o $@

clean:
	rm *.o *.gp main outfile.ppm outfile.pgm
