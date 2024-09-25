#include<stdlib.h>
#include<string.h>

#ifndef __POPC__
#include<stdio.h>
#else
#include<print.h>
#endif

#include "format.h"

#define NO_ERROR 0
#define SYNTAX_ERROR 1
#define UNSUPPORTED_ERROR 2
#define OOM_ERROR 3

#define MAX_DHTVLC_NODES 512

// Constancts for computing a DCT
#define W1 2841
#define W2 2676
#define W3 2408
#define W5 1609
#define W6 1108
#define W7 565


#define THROW(e) do { jpg->error = e; return; } while (0)


typedef struct _DhtVlc {
  unsigned char tuple, num_bits;
} DhtVlc;

typedef struct _DhtVlcNode {
  unsigned short children[2];
  unsigned char tuple;
} DhtVlcNode;


typedef struct _ColourChannel {
  int id;
  int dq_id, ac_id, dc_id;
  int width, height;
  int samples_x, samples_y, stride;
  unsigned char *pixels;
  int dc_cumulative_val;
} ColourChannel;


typedef struct _JPG {
  const unsigned char *buf, *pos, *end;
  unsigned int size;
  unsigned short width, height;
  unsigned short num_blocks_x, num_blocks_y;
  unsigned short block_size_x, block_size_y;
  unsigned char num_channels;
  int error;
  ColourChannel channels[3];
  unsigned char *pixels;
  unsigned int max_pixels;
  DhtVlcNode vlc_trees[4][MAX_DHTVLC_NODES];
  unsigned char dq_tables[4][64];
  int restart_interval;
  unsigned int bufbits;
  unsigned char num_bufbits;
  int block_space[64];
  unsigned char* scratch_space;
  unsigned int scratch_size, scratch_pos;
} JPG;



void writeJPG(const unsigned char* pixels, unsigned width, unsigned height, const char* filename);
unsigned char* allocScratch(JPG* jpg, size_t size);
void skipBlock(JPG* jpg);
void decodeSOF(JPG* jpg);
void decodeDHT(JPG* jpg);
void decodeDQT(JPG *jpg);
void decodeDRI(JPG *jpg);
unsigned short read16(const unsigned char *pos);
int showBits(JPG* jpg, int num_bits);
int getBits(JPG* jpg, int num_bits);
int getVLC(JPG* jpg, int dht_id, unsigned char* code);
void decodeBlock(JPG* jpg, ColourChannel* c, unsigned char* out);
void decodeScanCPU(JPG* jpg);
void iDCT_row(int* D);
void iDCT_col(const int* D, unsigned char *out, int stride);
void upsampleChannel(JPG* jpg, ColourChannel* channel);
void upsampleAndColourTransform(JPG* jpg);

static const char deZigZag[64] = {
  0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48,
  41, 34, 27, 20, 13, 6, 7, 14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15,
  23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
};



// ----------------------------------------------------------------------------------------------- //
// --------------------------------------- FORMAT ------------------------------------------------ //
// ----------------------------------------------------------------------------------------------- //


#ifndef __POPC__
void writeJPG(const unsigned char* pixels, unsigned width, unsigned height, const char* filename) {
  FILE* f = fopen(filename, "wb");
   if (!f) {
     printf("Couldn't open output file %s\n", filename);
     return;
   }
   fprintf(f, "P%d\n%d %d\n255\n", 6 , width, height);
   fwrite(pixels, 1, width * height * 3, f);
   fclose(f);
}
#endif


unsigned short read16(const unsigned char *pos) {
    return (pos[0] << 8) | pos[1];
}


void skipBlock(JPG* jpg){
  jpg->pos += read16(jpg->pos);
}


unsigned char* allocScratch(JPG* jpg, size_t size) {
  if (jpg->scratch_pos + size >= jpg->scratch_size) return NULL;

  unsigned char* ret = &jpg->scratch_space[jpg->scratch_pos];
  // 4-byte align for next allocation
  unsigned new_pos = jpg->scratch_pos + size + 3;
  new_pos -= new_pos % 4;
  jpg->scratch_pos = new_pos;

  return ret;
}


void decodeSOF(JPG* jpg){
  const unsigned char* block = jpg->pos;
  unsigned int block_len = read16(block);
  if(block_len < 9 || block + block_len >= jpg->end)
    THROW(SYNTAX_ERROR);
  if(block[2] != 8)
    THROW(UNSUPPORTED_ERROR);

  // Read image info //
  jpg->height = read16(block+3);
  jpg->width = read16(block+5);
  if(!jpg->width || !jpg->height)
    THROW(SYNTAX_ERROR);
  jpg->num_channels = block[7];
  if(jpg->num_channels != 1 && jpg->num_channels != 3)
    THROW(UNSUPPORTED_ERROR);

  // Read channel info //
  if (block_len < 8 + (jpg->num_channels * 3))
    THROW(SYNTAX_ERROR);
  block += 8;
  int i, samples_x_max = 0, samples_y_max = 0;
  ColourChannel *chan = jpg->channels;
  for(i = 0; i < jpg->num_channels; i++, chan++, block += 3){
    chan->id = block[0];
    chan->samples_x = block[1] >> 4;
    chan->samples_y = block[1] & 0xF;
    chan->dq_id = block[2];
    
    if(!chan->samples_x || !chan->samples_y || chan->dq_id > 3)
      THROW(SYNTAX_ERROR);
    if((chan->samples_x & (chan->samples_x - 1)) ||
       (chan->samples_y & (chan->samples_y - 1)))
      THROW(UNSUPPORTED_ERROR); // require power of two
    if(chan->samples_x > samples_x_max) samples_x_max = chan->samples_x;
    if(chan->samples_y > samples_y_max) samples_y_max = chan->samples_y;
  }
  
  if (jpg->num_channels == 1){
    jpg->channels[0].samples_x = samples_x_max = 1;
    jpg->channels[0].samples_y = samples_y_max = 1;
  }

  // Compute dimensions in blocks and allocate output space //
  jpg->block_size_x = samples_x_max << 3;
  jpg->block_size_y = samples_y_max << 3;
  jpg->num_blocks_x = (jpg->width + jpg->block_size_x -1) / jpg->block_size_x;
  jpg->num_blocks_y = (jpg->height + jpg->block_size_y -1) / jpg->block_size_y;
  
  for(i = 0, chan = jpg->channels; i < jpg->num_channels; i++, chan++){
    chan->width = (jpg->width * chan->samples_x + samples_x_max -1) / samples_x_max;
    chan->height = (jpg->height * chan->samples_y + samples_y_max -1) / samples_y_max;
    chan->stride = jpg->num_blocks_x * (chan->samples_x << 3);
    
    if(((chan->width < 3) && (chan->samples_x != samples_x_max)) ||
       ((chan->height < 3) && (chan->samples_y != samples_y_max)))
      THROW(UNSUPPORTED_ERROR);
    
    chan->pixels = allocScratch(jpg, chan->stride * jpg->num_blocks_y * (chan->samples_y << 3));
    if(!chan->pixels) THROW(OOM_ERROR);
  }
  if(jpg->num_channels == 3){
    // jpg->pixels = malloc(jpg->width * jpg->height * 3);
    // if(!jpg->pixels) THROW(OOM_ERROR);
    if (jpg->width * jpg->height > jpg->max_pixels) THROW(OOM_ERROR);
  } else {
    THROW(UNSUPPORTED_ERROR); // Disable greyscale for this project
  }
    
  jpg->pos += block_len;
}

// // 'code' contains 'num_bits' bits, starting with MSB! //
void addDhtLeaf(JPG* jpg, DhtVlcNode* nodes, int* num_nodes, unsigned short code, int num_bits, unsigned char payload) {
  unsigned current_node = 0;
  for(int bit_pos = 15; bit_pos >= 16 - num_bits; --bit_pos) {
    unsigned bit = (code >> bit_pos) & 1u;
    unsigned next_node = nodes[current_node].children[bit];
    if (next_node == 0) {
      // Child node doesn't exist, create it //
      next_node = (*num_nodes)++;
      if(next_node >= MAX_DHTVLC_NODES) THROW(UNSUPPORTED_ERROR);
      nodes[next_node] = (DhtVlcNode){{0, 0}, 0};
      nodes[current_node].children[bit] = next_node;
    }
    current_node = next_node;
  }
  nodes[current_node].tuple = payload;
}


void decodeDHT(JPG* jpg){
  const unsigned char* pos = jpg->pos;
  unsigned int block_len = read16(pos);
  const unsigned char *block_end = pos + block_len;
  if(block_end >= jpg->end) THROW(SYNTAX_ERROR);
  pos += 2;
  
  while(pos < block_end){
    unsigned char val = pos[0];
    if (val & 0xEC) THROW(SYNTAX_ERROR);
    if (val & 0x02) THROW(UNSUPPORTED_ERROR);
    unsigned char table_id = (val | (val >> 3)) & 3; // AC and DC

    // Decode as tree structure
    DhtVlcNode* dht_tree = &jpg->vlc_trees[table_id][0];
    int num_tree_nodes = 1;
    unsigned short huffman_code = 0;
    const unsigned char *tuple = pos + 17;
    for (int code_len = 1; code_len <= 16; code_len++) {
      int count = pos[code_len];
      if (!count) continue;
      if (tuple + count > block_end) THROW(SYNTAX_ERROR);
      for (int i = 0; i < count; i++) {
        addDhtLeaf(jpg, dht_tree, &num_tree_nodes, huffman_code, code_len, *tuple);
        if (jpg->error) return;
        huffman_code += 1 << (16 - code_len);
        tuple++;
      }
    }

    pos = tuple;
  }
  
  if (pos != block_end) THROW(SYNTAX_ERROR);
  jpg->pos = block_end;
}


void decodeDRI(JPG *jpg){
  unsigned int block_len = read16(jpg->pos);
  const unsigned char *block_end = jpg->pos + block_len;
  if ((block_len < 2) || (block_end >= jpg->end)) THROW(SYNTAX_ERROR);
  jpg->restart_interval = read16(jpg->pos + 2);
  jpg->pos = block_end; 
}


void decodeDQT(JPG *jpg){
  unsigned int block_len = read16(jpg->pos);
  const unsigned char *block_end = jpg->pos + block_len;
  if (block_end >= jpg->end) THROW(SYNTAX_ERROR);
  const unsigned char *pos = jpg->pos + 2;

  while(pos + 65 <= block_end){
    unsigned char table_id = pos[0];
    if (table_id & 0xFC) THROW(SYNTAX_ERROR);
    unsigned char *table = &jpg->dq_tables[table_id][0];
    memcpy(table, pos+1, 64);
    pos += 65;
  }
  if (pos != block_end) THROW(SYNTAX_ERROR);
  jpg->pos = block_end;
}



int readJPG(
  const unsigned char *inbuf, unsigned insize,
  unsigned char *outbuf, unsigned outsize,
  unsigned char *scratchbuf, unsigned scratchsize,
  const char* outfile // Leave NULL to not save
) {

  // Create jpg object
  JPG jpg = {0};
  if (insize < 6) return 0;

  jpg.buf = inbuf;
  jpg.size = insize;
  jpg.pixels = outbuf;
  jpg.max_pixels = outsize;
  jpg.scratch_space = scratchbuf;
  jpg.scratch_size = scratchsize;
  jpg.scratch_pos = 0;
  jpg.end = jpg.buf + insize;
  jpg.pos = jpg.buf + 2;
  jpg.error = NO_ERROR;

  // Check Magics //
  if((jpg.buf[0]        != 0xFF) || (jpg.buf[1]        != 0xD8) ||
     (jpg.buf[insize-2] != 0xFF) || (jpg.buf[insize-1] != 0xD9))
    return 0;

  // Main format block parsing loop //
  while(!jpg.error){
    if (jpg.pos > jpg.end - 2) {
      jpg.error = SYNTAX_ERROR;
      break;
    }
    if (jpg.pos[0] != 0xFF) {
      jpg.error = SYNTAX_ERROR;
      break;
    }
    
    jpg.pos += 2;
    switch(jpg.pos[-1]) {
    case 0xC0: decodeSOF(&jpg); break;
    case 0xC4: decodeDHT(&jpg); break;
    case 0xDB: decodeDQT(&jpg); break;
    case 0xDD: decodeDRI(&jpg); break;
    case 0xDA: decodeScanCPU(&jpg); break;
    case 0xFE: skipBlock(&jpg); break;
    case 0xD9: break;
    default:
      if((jpg.pos[-1] & 0xF0) == 0xE0) skipBlock(&jpg);
      else jpg.error = SYNTAX_ERROR;
    }

    // Finished //
    if (jpg.pos[-1] == 0xD9) {
      upsampleAndColourTransform(&jpg);
      break;
    }
  }

  if(jpg.error) {
    printf("Decode failed with error code %d\n", jpg.error);
    return 0;
  }


  #ifndef __POPC__
  if (outfile) {
    FILE* f = fopen(outfile, "wb");
    if (!f) {
      printf("Couldn't open output file %s\n", outfile);
      return 0;
    }
    fprintf(f, "P%d\n%d %d\n255\n", 6 , jpg.width, jpg.height);
    fwrite(jpg.pixels, 1, jpg.width * jpg.height * 3, f);
    fclose(f);
  }
  #endif

  return 1;
}


// This only shows the bits, but doesn't move past them //
int showBits(JPG* jpg, int num_bits){
  unsigned char newbyte;
  if(!num_bits) return 0;

  while (jpg->num_bufbits < num_bits){
    if(jpg->pos >= jpg->end){
      jpg->bufbits = (jpg->bufbits << 8) | 0xFF;
      jpg->num_bufbits += 8;
      continue;
    }
    newbyte = *jpg->pos++;
    jpg->bufbits = (jpg->bufbits << 8) | newbyte;
    jpg->num_bufbits += 8;
    if (newbyte != 0xFF)
      continue;
	
    if(jpg->pos >= jpg->end)
      goto overflow_error;
    
    // Handle byte stuffing //
    unsigned char follow_byte = *jpg->pos++;
    switch (follow_byte){
    case 0x00:
    case 0xFF:
    case 0xD9:
      break;
    default:
      if ((follow_byte & 0xF8) != 0xD0){
	printf("The follow_byte case that doesn't have to be 0x00?\n");
	goto overflow_error;
      } else {
	printf("The acceptable non-zero followbyte case?\n");
	jpg->bufbits = (jpg->bufbits << 8) | newbyte;
	jpg->num_bufbits += 8;
      }
    }
  }
  return (jpg->bufbits >> (jpg->num_bufbits - num_bits)) & ((1 << num_bits) - 1);

 overflow_error:
  printf("Huffman decode overflow?\n");
  jpg->error = SYNTAX_ERROR;
  return 0;
}

// ----------------------------------------------------------------------------------------------- //
// --------------------------------------- DECODE SCAN ------------------------------------------- //
// ----------------------------------------------------------------------------------------------- //


// Show the bits AND move past them //
int getBits(JPG* jpg, int num_bits){
  int res = showBits(jpg, num_bits);
  jpg->num_bufbits -= num_bits;
  return res;
}


int getVLC(JPG* jpg, int dht_id, unsigned char* code){

  // Decode huffman tree
  int bits = showBits(jpg, 16);
  DhtVlcNode *tree = &jpg->vlc_trees[dht_id][0];
  unsigned current_node = 0;
  int bits_used = 0;
  while (bits_used < 16) {
    int bit = (bits >> (15 - bits_used)) & 1u;
    current_node = tree[current_node].children[bit];
    bits_used += 1;
    if (tree[current_node].children[0] == 0) break;
  }
  unsigned char tuple = tree[current_node].tuple;
  jpg->num_bufbits -= bits_used;

  // Extract value bit according to tuple
  if(code) *code = tuple;
  unsigned char val_bits = tuple & 0x0F;
  if (!val_bits) return 0;
  int value = getBits(jpg, val_bits);
  if (value < (1 << (val_bits - 1)))
    value += ((-1) << val_bits) + 1;
  return value; 
}


void decodeBlock(JPG* jpg, ColourChannel* channel, unsigned char* out){
  unsigned char code = 0;
  int value, coef = 0;
  int* block = jpg->block_space;
  memset(block, 0, 64 * sizeof(int));

  // Read DC value //
  channel->dc_cumulative_val += getVLC(jpg, channel->dc_id, NULL);
  block[0] = (channel->dc_cumulative_val) * jpg->dq_tables[channel->dq_id][0];
  // Read  AC values //
  do {
    value = getVLC(jpg, channel->ac_id, &code);
    if (!code) break; // EOB marker //
    if (!(code & 0x0F) && (code != 0xF0)) THROW(SYNTAX_ERROR);
    coef += (code >> 4) + 1;
    if (coef > 63) THROW(SYNTAX_ERROR);
    block[(int)deZigZag[coef]] = value * jpg->dq_tables[channel->dq_id][coef];
  } while(coef < 63);

  // Invert the DCT //
  for (coef = 0;  coef < 64;  coef += 8)
    iDCT_row(&block[coef]);
  for (coef = 0;  coef < 8;  ++coef)
  iDCT_col(&block[coef], &out[coef], channel->stride);
}


void decodeScanCPU(JPG* jpg){
  const unsigned char *pos = jpg->pos;
  unsigned int header_len = read16(pos);
  if (pos + header_len >= jpg->end) THROW(SYNTAX_ERROR);
  pos += 2;

  if (header_len < (4 + 2 * jpg->num_channels)) THROW(SYNTAX_ERROR);
  if (*(pos++) != jpg->num_channels) THROW(UNSUPPORTED_ERROR);
  int i; ColourChannel *channel;
  for(i = 0, channel=jpg->channels; i<jpg->num_channels; i++, channel++, pos+=2){
    if (pos[0] != channel->id) THROW(SYNTAX_ERROR);
    if (pos[1] & 0xEE) THROW(SYNTAX_ERROR);
    channel->dc_id = pos[1] >> 4;
    channel->ac_id = (pos[1] & 1) | 2;
  }
  if (pos[0] || (pos[1] != 63) || pos[2]) THROW(UNSUPPORTED_ERROR);
  pos = jpg->pos = jpg->pos + header_len;


  int restart_interval = jpg->restart_interval;
  int restart_count = restart_interval;
  int next_restart_index = 0;
  
  // Loop over all blocks
  for (int block_y = 0; block_y < jpg->num_blocks_y; block_y++){
    for (int block_x = 0; block_x < jpg->num_blocks_x; block_x++){

      // Loop over all channels //
      for (i = 0, channel = jpg->channels; i < jpg->num_channels; i++, channel++){

        // Loop over samples in block //
        for (int sample_y = 0; sample_y < channel->samples_y; ++sample_y){
          for (int sample_x = 0; sample_x < channel->samples_x; ++sample_x){
            
            int out_pos = ((block_y * channel->samples_y + sample_y) * channel->stride
              + block_x * channel->samples_x + sample_x) << 3;
            decodeBlock(jpg, channel, &channel->pixels[out_pos]);
            if (jpg->error) return;
          }
        }
      }

      if (restart_interval && !(--restart_count)){
        // Byte align //
        jpg->num_bufbits &= 0xF8;
        i = getBits(jpg, 16);
        if (((i & 0xFFF8) != 0xFFD0) || ((i & 7) != next_restart_index)) 
          THROW(SYNTAX_ERROR);
        next_restart_index = (next_restart_index + 1) & 7;
        restart_count = restart_interval;
        for (i = 0; i < 3; i++)
          jpg->channels[i].dc_cumulative_val = 0;
      }
    }
  }
 
}


// --------------------------------------------------------------------------------------------------- //
// --------------------------------------- PIXEL TRANSFORM ------------------------------------------- //
// --------------------------------------------------------------------------------------------------- //


// inline 
unsigned char clip(const int x) {
  return (x < 0) ? 0 : ((x > 0xFF) ? 0xFF : (unsigned char) x);
}


void iDCT_row(int* D) {
    int x0, x1, x2, x3, x4, x5, x6, x7, x8;
    if (!((x1 = D[4] << 11)
        | (x2 = D[6])
        | (x3 = D[2])
        | (x4 = D[1])
        | (x5 = D[7])
        | (x6 = D[5])
        | (x7 = D[3])))
    {
        D[0] = D[1] = D[2] = D[3] = D[4] = D[5] = D[6] = D[7] = D[0] << 3;
        return;
    }
    x0 = (D[0] << 11) + 128;
    x8 = W7 * (x4 + x5);
    x4 = x8 + (W1 - W7) * x4;
    x5 = x8 - (W1 + W7) * x5;
    x8 = W3 * (x6 + x7);
    x6 = x8 - (W3 - W5) * x6;
    x7 = x8 - (W3 + W5) * x7;
    x8 = x0 + x1;
    x0 -= x1;
    x1 = W6 * (x3 + x2);
    x2 = x1 - (W2 + W6) * x2;
    x3 = x1 + (W2 - W6) * x3;
    x1 = x4 + x6;
    x4 -= x6;
    x6 = x5 + x7;
    x5 -= x7;
    x7 = x8 + x3;
    x8 -= x3;
    x3 = x0 + x2;
    x0 -= x2;
    x2 = (181 * (x4 + x5) + 128) >> 8;
    x4 = (181 * (x4 - x5) + 128) >> 8;
    D[0] = (x7 + x1) >> 8;
    D[1] = (x3 + x2) >> 8;
    D[2] = (x0 + x4) >> 8;
    D[3] = (x8 + x6) >> 8;
    D[4] = (x8 - x6) >> 8;
    D[5] = (x0 - x4) >> 8;
    D[6] = (x3 - x2) >> 8;
    D[7] = (x7 - x1) >> 8;
}


void iDCT_col(const int* D, unsigned char *out, int stride) {
    int x0, x1, x2, x3, x4, x5, x6, x7, x8;
    if (!((x1 = D[8*4] << 8)
        | (x2 = D[8*6])
        | (x3 = D[8*2])
        | (x4 = D[8*1])
        | (x5 = D[8*7])
        | (x6 = D[8*5])
        | (x7 = D[8*3])))
    {
        x1 = clip(((D[0] + 32) >> 6) + 128);
        for (x0 = 8;  x0;  --x0) {
            *out = (unsigned char) x1;
            out += stride;
        }
        return;
    }
    x0 = (D[0] << 8) + 8192;
    x8 = W7 * (x4 + x5) + 4;
    x4 = (x8 + (W1 - W7) * x4) >> 3;
    x5 = (x8 - (W1 + W7) * x5) >> 3;
    x8 = W3 * (x6 + x7) + 4;
    x6 = (x8 - (W3 - W5) * x6) >> 3;
    x7 = (x8 - (W3 + W5) * x7) >> 3;
    x8 = x0 + x1;
    x0 -= x1;
    x1 = W6 * (x3 + x2) + 4;
    x2 = (x1 - (W2 + W6) * x2) >> 3;
    x3 = (x1 + (W2 - W6) * x3) >> 3;
    x1 = x4 + x6;
    x4 -= x6;
    x6 = x5 + x7;
    x5 -= x7;
    x7 = x8 + x3;
    x8 -= x3;
    x3 = x0 + x2;
    x0 -= x2;
    x2 = (181 * (x4 + x5) + 128) >> 8;
    x4 = (181 * (x4 - x5) + 128) >> 8;
    *out = clip(((x7 + x1) >> 14) + 128);  out += stride;
    *out = clip(((x3 + x2) >> 14) + 128);  out += stride;
    *out = clip(((x0 + x4) >> 14) + 128);  out += stride;
    *out = clip(((x8 + x6) >> 14) + 128);  out += stride;
    *out = clip(((x8 - x6) >> 14) + 128);  out += stride;
    *out = clip(((x0 - x4) >> 14) + 128);  out += stride;
    *out = clip(((x3 - x2) >> 14) + 128);  out += stride;
    *out = clip(((x7 - x1) >> 14) + 128);
}


void upsampleChannel(JPG* jpg, ColourChannel* channel) {
    int x, y, xshift = 0, yshift = 0;
    unsigned char *out, *lout;
    while (channel->width < jpg->width) { channel->width <<= 1; ++xshift; }
    while (channel->height < jpg->height) { channel->height <<= 1; ++yshift; }
    out = allocScratch(jpg, channel->width * channel->height);
    if (!out) THROW(OOM_ERROR);
    
    for (y = 0, lout = out;  y < channel->height;  ++y, lout += channel->width) {
        unsigned char *lin = &channel->pixels[(y >> yshift) * channel->stride];
        for (x = 0;  x < channel->width;  ++x)
            lout[x] = lin[x >> xshift];
    }
    
    channel->stride = channel->width;
    // free(channel->pixels);
    channel->pixels = out;
}


void upsampleAndColourTransform(JPG* jpg) {
  int i;
  ColourChannel* channel;
  for (i = 0, channel = &jpg->channels[0];  i < jpg->num_channels;  ++i, ++channel) {
    if ((channel->width < jpg->width) || (channel->height < jpg->height))
      upsampleChannel(jpg, channel);
    if ((channel->width < jpg->width) || (channel->height < jpg->height)){
      printf("Logical error?\n");
      THROW(SYNTAX_ERROR);
    }
  }
  if (jpg->num_channels == 3) {
    // convert to RGB //
    unsigned char *prgb = jpg->pixels;
    const unsigned char *py  = jpg->channels[0].pixels;
    const unsigned char *pcb = jpg->channels[1].pixels;
    const unsigned char *pcr = jpg->channels[2].pixels;
    for (int yy = jpg->height;  yy;  --yy) {
      for (int x = 0;  x < jpg->width;  ++x) {
        register int y = py[x] << 8;
        register int cb = pcb[x] - 128;
        register int cr = pcr[x] - 128;
        *prgb++ = clip((y            + 359 * cr + 128) >> 8);
        *prgb++ = clip((y -  88 * cb - 183 * cr + 128) >> 8);
        *prgb++ = clip((y + 454 * cb            + 128) >> 8);
      }
      py += jpg->channels[0].stride;
      pcb += jpg->channels[1].stride;
      pcr += jpg->channels[2].stride;
    }
  } else if (jpg->channels[0].width != jpg->channels[0].stride) {
    // grayscale -> only remove stride
    ColourChannel *channel = &jpg->channels[0];
    unsigned char *pin = &channel->pixels[channel->stride];
    unsigned char *pout = &channel->pixels[channel->width];
    for (int y = channel->height - 1;  y;  --y) {
      memcpy(pout, pin, channel->width);
      pin += channel->stride;
      pout += channel->width;
    }
    channel->stride = channel->width;
  }
}
