#include <print.h>
#include <poplar/Vertex.hpp>

#include"format.h"
#include "ipuInterface.h"


void fade(unsigned char* start, unsigned char* end, unsigned N, float progress) {
    for (int i = 0; i < N * 3; ++i) {
        start[i] = (1 - progress) * start[i] + progress * end[i];
    }
}

void localhwipe(unsigned char* start, unsigned char* end, unsigned w, unsigned h, float progress) {
    unsigned thresh = progress * w;
    for (unsigned y = 0; y < h; ++y) {
        unsigned char* start_row = &start[y * 3 * w];
        unsigned char* end_row = &end[y * 3 * w];
        start_row[thresh*3] -= 1;
        start_row[thresh*3 + 1] -=1 ;
        start_row[thresh*3 + 2] -= 1;
        for (unsigned x = 0; x < thresh * 3; ++x, start_row++, end_row++) {
            *start_row = *end_row;
        }
    }
}

void dissolve(unsigned char* start, unsigned char* end, unsigned N, float progress) {
    
}


class Decoder : public poplar::Vertex {
   public:
    poplar::Input<poplar::Vector<unsigned char>> files;
    poplar::Input<poplar::Vector<unsigned>> starts;
    poplar::Input<poplar::Vector<unsigned>> lengths;
    poplar::Input<poplar::Vector<unsigned char>> requestBuf;
    poplar::Input<unsigned> width;
    poplar::Output<poplar::Vector<unsigned char>> pixels;
    poplar::Output<poplar::Vector<unsigned char>> scratch;
    poplar::Output<poplar::Vector<unsigned char>> transitionPixels;
    

    void compute() {

        IPURequest_t* request = (IPURequest_t*) &requestBuf[0];

        int success = readJPG(
            &files[starts[request->currentImage]], 
            lengths[request->currentImage],
            &pixels[0], pixels.size(),
            &scratch[0], scratch.size(),
            NULL
        );

        if (request->transitionImage != -1) {
            success &= readJPG(
                &files[starts[request->transitionImage]], 
                lengths[request->transitionImage],
                &transitionPixels[0], transitionPixels.size(),
                &scratch[0], scratch.size(),
                NULL
            );

            float progress = request->transitionFrame / (float) request->transitionLength;
            switch (request->transition) {
                case FADE:
                    fade(&pixels[0], &transitionPixels[0], pixels.size() / 3, progress);
                    break;
                case LOCAL_H_WIPE:
                    localhwipe(&pixels[0], &transitionPixels[0], width, pixels.size() / (width * 3), progress);
                    break;
                default:
                    printf("Unhandled transition\n");
            }
        }

        if (!success) printf("JPG decode error");
        return;
    }
};
