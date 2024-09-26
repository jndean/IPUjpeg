#include <print.h>
#include <poplar/Vertex.hpp>

#include"format.h"
#include "ipuInterface.h"



class Decoder : public poplar::Vertex {
   public:
    poplar::Input<poplar::Vector<unsigned char>> files;
    poplar::Input<poplar::Vector<unsigned>> starts;
    poplar::Input<poplar::Vector<unsigned>> lengths;
    poplar::Input<poplar::Vector<unsigned char>> requestBuf;
    poplar::Output<poplar::Vector<unsigned char>> pixels;
    poplar::Output<poplar::Vector<unsigned char>> scratch;
    

    void compute() {

        IPURequest_t* request = (IPURequest_t*) &requestBuf[0];
        int currentImage = request->currentImage;


        int success = readJPG(
            &files[starts[currentImage]], lengths[currentImage],
            &pixels[0], pixels.size(),
            &scratch[0], scratch.size(),
            NULL
        );
        if (!success) printf("JPG decode error");
        

        return;
    }
};
