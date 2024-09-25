#ifndef FORMAT_H
#define FORMAT_H
#ifdef __cplusplus
extern "C" {
#endif



int readJPG(
  const unsigned char *inbuf, unsigned insize,
  unsigned char *outbuf, unsigned outsize,
  unsigned char *scratchbuf, unsigned scratchsize,
  const char* outfile
);


#ifdef __cplusplus
}
#endif
#endif // FORMAT_H //
