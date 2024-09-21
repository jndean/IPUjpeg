#include<stdio.h>
#include<stdlib.h>
#include<time.h>

#include"format.h"


int main(int argc, char** argv){

  size_t outbufsize = 320 * 200 * 3;
  size_t scratchsize = 200000;

  
  if(argc < 2) {
    fprintf(stderr, "USAGE: %s filename.jpg ...\n", argv[0]);
    return EXIT_FAILURE;
  }
  
  FILE* f = NULL;
  unsigned int inbufsize = 0;

  // Read file //
  f = fopen(argv[1], "r");
  if (NULL == f) goto failure;
  fseek(f, 0, SEEK_END);
  inbufsize = ftell(f);
  unsigned char* inbuf = malloc(inbufsize);
  unsigned char* scratchbuf = malloc(scratchsize);
  unsigned char* outbuf = malloc(outbufsize);
  fseek(f, 0, SEEK_SET);
  if(fread(inbuf, 1, inbufsize, f) != inbufsize) goto failure;
  fclose(f);


  int success = readJPG(inbuf, inbufsize, outbuf, outbufsize, scratchbuf, scratchsize, "outfile.ppm");
  if (!success) return EXIT_FAILURE;

  free(inbuf);
  free(scratchbuf);
  free(outbuf);
  
  return EXIT_SUCCESS;

failure:
    printf("Main Failure\n");
    return EXIT_FAILURE;
}
