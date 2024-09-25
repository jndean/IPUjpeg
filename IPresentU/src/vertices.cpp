#include <print.h>
#include <poplar/Vertex.hpp>

#include"format.h"
#include "ipuInterface.h"



class Decoder : public poplar::Vertex {
   public:
    poplar::Input<poplar::Vector<unsigned char>> files;
    poplar::Input<poplar::Vector<unsigned>> starts;
    poplar::Input<poplar::Vector<unsigned>> lengths;
    poplar::Input<poplar::Vector<unsigned char>> stateBuf;
    poplar::Output<poplar::Vector<unsigned char>> pixels;
    poplar::Output<poplar::Vector<unsigned char>> scratch;
    

    void compute() {

        SlidesState_t* state = (SlidesState_t*) &stateBuf[0];
        int currentSlide = state->currentSlide;

        int success = readJPG(
            &files[starts[currentSlide]], lengths[currentSlide],
            &pixels[0], pixels.size(),
            &scratch[0], scratch.size(),
            NULL
        );
        if (!success) printf("JPG decode error");
        

        return;
    }
};
