
SRCDIR   = src
INCDIR   = include
OBJDIR   = obj
BINDIR   = bin

CFLAGS   = -std=c99 -Wall -I${INCDIR} -O3

default: src/main.cpp codelets.gp
	g++ src/main.cpp -I /usr/local/include/SDL2 -I/opt/poplar/include -Iinclude -lSDL2 -lpoplar -Wall -Wextra -O2 -o main

codelets.gp: src/vertices.cpp src/format.c include/ipuInterface.h
	popc src/vertices.cpp -I include -o vertices.gp
	popc src/format.c -I include -o format.gp
	popc vertices.gp format.gp -o codelets.gp


%.o: $(SRCDIR)/%.c
	gcc $(CFLAGS) -c $< -o $@

cpu: cpu.o format.o
	gcc ${CFLAGS} $^ -o cpu

clean:
	rm *.o *.gp
