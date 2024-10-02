#include <print.h>
#include <cmath>
#include <poplar/TileConstants.hpp>
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


void jazz(unsigned char* pixels, unsigned N, float hue) {
    
    int i = hue * 6;
    float f = hue * 6 - i;
    int p = 0;
    int q = 255 * (1 - f);
    int t = 255 - q;

    int r, g, b;
    switch (i % 6) {
        case 0: r = 256, g = t, b = 0; break;
        case 1: r = q, g = 256, b = 0; break;
        case 2: r = 0, g = 256, b = t; break;
        case 3: r = 0, g = q, b = 256; break;
        case 4: r = t, g = 0, b = 256; break;
        case 5: r = 256, g = 0, b = q; break;
    }

    for (int i = 0; i < N; i += 3) {
        pixels[i + 0] = (pixels[i + 0] + r) % 256;
        pixels[i + 1] = (pixels[i + 1] + g) % 256;
        pixels[i + 2] = (pixels[i + 2] + b) % 256;
        // pixels[i + 0] = 0;
    }
}


class Decoder : public poplar::Vertex {
   public:
    poplar::Input<poplar::Vector<unsigned char>> files;
    poplar::Input<poplar::Vector<unsigned>> starts;
    poplar::Input<poplar::Vector<unsigned>> lengths;
    poplar::Input<poplar::Vector<unsigned char>> requestBuf;
    poplar::Input<unsigned> width;
    poplar::Input<unsigned> patchId;
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

        static int jazz_count = -1;
        if (request->jazz) {
            if (jazz_count == -1) jazz_count = (*patchId * 23);
            int period = 151;
            jazz_count = (jazz_count + 1) % period;
            jazz(&pixels[0], pixels.size(), jazz_count / (float) period);
        }

        if (!success) printf("JPG decode error");
        return;
    }
};
