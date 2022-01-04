TARGET   = main

SRCDIR   = ./
INCDIR   = ./
OBJDIR   = ./
BINDIR   = ./

SOURCES  := $(wildcard $(SRCDIR)/*.c)
INCLUDES := $(wildcard $(INCDIR)/*.h)
OBJECTS  := $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

CFLAGS   = -Wall -I${INCDIR} -O3

default: main.o JPGReader.o JPGReader_UpsampleColourTransform.o JPGReader_decodescan.o
	g++ ${CFLAGS} $^ -o ${TARGET}

main.o: main.cpp
	g++ $(CFLAGS) -c $< -o $@

%.o: $(SRCDIR)/%.c $(INCDIR)/%.h
	g++ $(CFLAGS) -c $< -o $@

clean:
	rm *.o ${TARGET} outfile.ppm outfile.pgm
