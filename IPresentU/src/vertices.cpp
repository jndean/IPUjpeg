#include <print.h>
#include <poplar/Vertex.hpp>

#include"format.h"
#include "ipuInterface.h"


int pos = 0;

class Decoder : public poplar::Vertex {
   public:
    poplar::Input<poplar::Vector<unsigned char>> files;
    poplar::Input<poplar::Vector<unsigned>> starts;
    poplar::Input<poplar::Vector<unsigned>> lengths;
    poplar::Output<poplar::Vector<unsigned char>> pixels;
    poplar::Output<poplar::Vector<unsigned char>> scratch;

    void compute() {

        int success = readJPG(
            &files[starts[pos]], lengths[pos],
            &pixels[0], pixels.size(),
            &scratch[0], scratch.size(),
            NULL
        );
        if (!success) printf("JPG decode error");

        pos = (pos + 1) % starts.size();
        

        return;
    }
};
