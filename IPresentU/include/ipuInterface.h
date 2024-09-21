#ifndef __IPUINTERFACE__
#define __IPUINTERFACE__
#ifdef __cplusplus
extern "C" {
#endif


#define JPGSPERTILE (1)


typedef struct {
    unsigned starts[JPGSPERTILE];
    unsigned lengths[JPGSPERTILE];
    unsigned char buf[0];
} DecoderTransfer_t;



#ifdef __cplusplus
}
#endif
#endif // __IPUINTERFACE__ //