#include <print.h>
#include <poplar/Vertex.hpp>

#include"format.h"
#include "ipuInterface.h"


int pos = 0;

class Decoder : public poplar::Vertex {
   public:
    poplar::Input<poplar::Vector<unsigned char>> files;
    poplar::Output<poplar::Vector<unsigned char>> pixels;
    poplar::Output<poplar::Vector<unsigned char>> scratch;

    void compute() {
        DecoderTransfer_t* infiles = (DecoderTransfer_t*) &files[0];

        for(int i = 0; i < 120; ++i) {
            pixels[pos] = 255;
            pos = (pos + 3) % (pixels.size() - 1);
        }
        int success = readJPG(
            &infiles->buf[infiles->starts[0]], infiles->lengths[0],
            &pixels[0], pixels.size(),
            &scratch[0], scratch.size(),
            NULL
        );


        return;
    }
};
