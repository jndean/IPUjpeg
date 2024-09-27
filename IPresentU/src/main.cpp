// g++ src/IPresentU.cpp -I /usr/local/include/SDL2 -I/opt/poplar/include -lSDL2 -lpoplar -Wall  -Wextra -O2 -o IPresentU

#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <iterator>
#include <vector>
#include <stdexcept>

#include <SDL2/SDL.h>
#include <poplar/DeviceManager.hpp>
#include <poplar/Engine.hpp>
#include <poplar/IPUModel.hpp>

#include "ipuInterface.h"


void UpdateFrameTiming( std::ostream& os = std::cout, float period = 2.0f )
{
    static unsigned int frames = 0;
    frames++;
    static auto start = std::chrono::steady_clock::now();
    auto end = std::chrono::steady_clock::now();

    auto seconds = std::chrono::duration< float >( end - start ).count();
    if( seconds >= period )
    {
        os
            << frames << " frames in "
            << std::setprecision( 1 ) << std::fixed << seconds << " seconds = "
            << std::setprecision( 1 ) << std::fixed << frames / seconds << " FPS ("
            << std::setprecision( 3 ) << std::fixed << seconds / frames * 1000.0 << " ms/frame)\n";
        frames = 0;
        start = end;
    }
}


poplar::Device getIPU(bool use_hardware, int num_ipus=1) {

  if (use_hardware) {
auto manager = poplar::DeviceManager::createDeviceManager();
    auto devices = manager.getDevices(poplar::TargetType::IPU, num_ipus);
    auto it = std::find_if(devices.begin(), devices.end(), [](poplar::Device &device) {
	return device.attach();
      });
    if (it == devices.end()) {
      std::cerr << "Error attaching to device\n";
      exit(EXIT_FAILURE);
    }
    std::cout << "Attached to IPU " << it->getId() << std::endl;
    return std::move(*it);
    
  } else {
    poplar::IPUModel ipuModel;
    return ipuModel.createDevice(); 
  }
}


int readFile(const char* filename, unsigned char* inbuf, const size_t inbufsize) {
    size_t filesize;

    FILE* f = NULL;
    f = fopen(filename, "r");
    if (NULL == f) goto failure;
    fseek(f, 0, SEEK_END);
    filesize = ftell(f);
    if (filesize > inbufsize) goto failure;
    fseek(f, 0, SEEK_SET);
    if(fread(inbuf, 1, filesize, f) != filesize) goto failure;
    fclose(f);
    return filesize;

failure:
    if (NULL != f) fclose(f);
    return -1;
}


struct SlideDesc {
    unsigned numFrames;
    Transition_t transition;
    unsigned transitionFrames;
    unsigned firstImg;
    bool loops;
};


struct Slides {
    unsigned numImgs;
    unsigned width, height;
    unsigned block_w, block_h;
    unsigned blocks_x, blocks_y, numBlocks;
    unsigned fileBufSize;
    std::vector<unsigned char> files;
    std::vector<unsigned> starts;
    std::vector<unsigned> lengths;

    std::vector<SlideDesc> slideDescs;
    unsigned currentSlide;
    unsigned targetSlide;
    unsigned currentFrame;
    bool transitioning;
    unsigned transitionFrame;

    IPURequest_t ipuRequest;
    std::chrono::time_point<std::chrono::steady_clock> lastTick;

    Slides(const char* dir) {
        char filename[200];

        // Load metadata
        snprintf(filename, sizeof(filename), "%s/format.meta", dir);
        std::ifstream input(filename, std::ios::binary);
        std::vector<char> buffer(std::istreambuf_iterator<char>(input), {});

        unsigned* data = (unsigned*) buffer.data();
        numImgs = *(data++);
        width = *(data++);
        height = *(data++);
        block_w = *(data++);
        block_h = *(data++);
        blocks_x = *(data++);
        blocks_y = *(data++);
        numBlocks = blocks_x * blocks_y;
        for (unsigned firstImg = 0; firstImg < numImgs;) {
            SlideDesc desc;
            desc.numFrames = *(data++);
            desc.loops = *(data++);
            desc.transition = (Transition_t) *(data++);
            desc.transitionFrames = *(data++);
            desc.firstImg = firstImg;
            firstImg += desc.numFrames;
            slideDescs.push_back(desc);
        }


        // Load slide chunks
        printf("Finding files...\n");
        std::vector<int> filebufSizes(numBlocks, 0);
        for (unsigned y = 0, progress = 1; y < blocks_y; ++y) {
            for (unsigned x = 0; x < blocks_x; ++x) {
                for (unsigned img = 0; img < numImgs; ++img, ++progress) {
                    snprintf(filename, sizeof(filename), "%s/%d_%d_%d.jpg", dir, img, x, y);
                    struct stat stat_buf;
                    int rc = stat(filename, &stat_buf);
                    if (rc) throw std::invalid_argument(filename);
                    filebufSizes[y * blocks_x + x] += stat_buf.st_size;
                    printf("\rFinding files: %3d/%d", progress, blocks_y * blocks_x * numImgs);
                }
            }
        }
        int maxBufSize = *std::max_element(filebufSizes.begin(), filebufSizes.end());
        fileBufSize = (maxBufSize + 3u) & (~3u); // 4-byte pad
        printf("\nDone, filebuf size on tile = %0.2lfKB\n", (fileBufSize) / 1e3);

        files = std::vector<unsigned char>(numBlocks * fileBufSize);
        starts = std::vector<unsigned>(numBlocks * numImgs);
        lengths = std::vector<unsigned>(numBlocks * numImgs);
        auto start_it = starts.begin();
        auto length_it = lengths.begin();

        for (unsigned y = 0, progress = 1; y < blocks_y; ++y) {
            for (unsigned x = 0; x < blocks_x; ++x) {
                unsigned bufOffset = fileBufSize * (y * blocks_x + x);
                unsigned bufPos = 0;
                for (unsigned img = 0; img < numImgs; ++img, ++progress) {
                    snprintf(filename, sizeof(filename), "%s/%d_%d_%d.jpg", dir, img, x, y);
                    FILE* f = fopen(filename, "r");
                    if (NULL == f) throw std::invalid_argument(filename);
                    fseek(f, 0, SEEK_END);
                    unsigned filesize = ftell(f);
                    fseek(f, 0, SEEK_SET);
                    if (fread(&files[bufOffset + bufPos], 1, filesize, f) != filesize) {
                        throw std::invalid_argument("Partial file read");
                    }
                    fclose(f);

                    *(start_it++) = bufPos;
                    *(length_it++) = filesize;
                    bufPos += filesize;
                    
                    printf("\rLoading files: %3d/%d", progress, blocks_y * blocks_x * numImgs);
                }
            }
        }
        printf("\nDone\n");

        // Prep animation state machine
        currentSlide = 0;
        targetSlide = 0;
        transitioning = false;
        currentFrame = 0;
        ipuRequest.currentImage = 0;
        ipuRequest.transitionImage = -1;
        lastTick = std::chrono::steady_clock::now();
    }

    void nextSlide() {
        targetSlide = std::min(targetSlide + 1, (unsigned) slideDescs.size() - 1);
        if (transitioning) return;
        
        currentFrame = 0;
        SlideDesc slide = slideDescs[currentSlide];
        if (slide.transition == INSTANT) {
            currentSlide = targetSlide;
        } else {
            transitioning = true;
            transitionFrame = 0;
        }
    }
    void prevSlide() {
        if (targetSlide) targetSlide -= 1;
        if (transitioning) return;

        currentSlide = targetSlide;
        currentFrame = 0;
    }

    void tick() {
        const auto now = std::chrono::steady_clock::now();
        auto secondsPassed = std::chrono::duration< float >(now - lastTick).count();
        lastTick = now;

        const int FPS = 20;
        static double frameRemainder = 0;
        double framesTodo = secondsPassed * FPS + frameRemainder;
        int wholeFramesTodo = (int)framesTodo;
        frameRemainder = framesTodo - wholeFramesTodo;

        SlideDesc slide = slideDescs[currentSlide];

        if (transitioning) {
            transitionFrame += wholeFramesTodo;
            if (transitionFrame >= slide.transitionFrames) {
                transitioning = false;
                wholeFramesTodo = std::max(0u, transitionFrame - slide.transitionFrames - 1);
                currentSlide = targetSlide;
                currentFrame = 0;
                slide = slideDescs[currentSlide];
            }
        }

        if (!transitioning) {
            if (slide.loops) currentFrame = (currentFrame + wholeFramesTodo) % slide.numFrames;
            else currentFrame = std::min(currentFrame + wholeFramesTodo, slide.numFrames - 1u);
            ipuRequest.currentImage = slide.firstImg + currentFrame;
            ipuRequest.transitionImage = -1; // Marks no transition
        } else {
            ipuRequest.currentImage = slide.firstImg + currentFrame;
            ipuRequest.transitionImage = slideDescs[targetSlide].firstImg;
            ipuRequest.transition = slide.transition;
            ipuRequest.transitionFrame = transitionFrame;
            ipuRequest.transitionLength = slide.transitionFrames;
        }



    }
};



int main(int argc, char** argv) {
    if(argc < 2) {
        fprintf(stderr, "USAGE: %s presentation/\n", argv[0]);
        return EXIT_FAILURE;
    }

    // Load presentation from disk
    Slides slides(argv[1]);

    const unsigned int bytesPerPixel = 3;
    const unsigned int pixelsSize = slides.width * slides.height * bytesPerPixel;
    const unsigned int numTiles = slides.numBlocks;
    const unsigned int scratchSize = 8192;

    const unsigned int windowWidth = 480; // or slides.width;
    const unsigned int windowHeight = 270; // or slides.height;

    // Setup IPU resources
    auto device = getIPU(true);
    poplar::Graph graph(device.getTarget());

    std::vector<unsigned char> pixels_h(pixelsSize, 0);
    poplar::Tensor pixels_d = graph.addVariable(poplar::UNSIGNED_CHAR, {2, slides.height, slides.width, bytesPerPixel}, "pixels");
    poplar::Tensor files_d = graph.addVariable(poplar::UNSIGNED_CHAR, {numTiles, slides.fileBufSize}, "files");
    poplar::Tensor starts_d = graph.addVariable(poplar::UNSIGNED_INT, {numTiles, slides.numImgs}, "starts");
    poplar::Tensor lengths_d = graph.addVariable(poplar::UNSIGNED_INT, {numTiles, slides.numImgs}, "lengths");
    poplar::Tensor scratch_d = graph.addVariable(poplar::UNSIGNED_CHAR, {numTiles, scratchSize}, "scratch");
    poplar::Tensor request_d = graph.addVariable(poplar::UNSIGNED_CHAR, {sizeof(IPURequest_t)}, "request");
    poplar::Tensor blockWidth_d = graph.addVariable(poplar::UNSIGNED_INT, {}, "blockWidth");
    poplar::DataStream pixelsStream = graph.addDeviceToHostFIFO("pixels-stream", poplar::UNSIGNED_CHAR, pixelsSize);
    poplar::DataStream requestStream = graph.addHostToDeviceFIFO("request-stream", poplar::UNSIGNED_CHAR, sizeof(IPURequest_t));
    poplar::DataStream filesStream = graph.addHostToDeviceFIFO("files-stream", poplar::UNSIGNED_CHAR, slides.files.size());
    poplar::DataStream startsStream = graph.addHostToDeviceFIFO("starts-stream", poplar::UNSIGNED_INT, slides.starts.size());
    poplar::DataStream lengthsStream = graph.addHostToDeviceFIFO("lengths-stream", poplar::UNSIGNED_INT, slides.lengths.size());

    graph.addCodelets("codelets.gp");
    poplar::ComputeSet mainCS = graph.addComputeSet("mainCS");

    for (unsigned y = 0, tile = 0; y < slides.blocks_y; ++y) {
        for (unsigned x = 0; x < slides.blocks_x; ++x, ++tile) {
            auto block = pixels_d.slice(
                {0, (y    ) * slides.block_h, (x    ) * slides.block_w},
                {2, (y + 1) * slides.block_h, (x + 1) * slides.block_w}
            );
            auto pixels = block[0].flatten();
            auto transitionPixels = block[1].flatten();
            graph.setTileMapping(block, tile);
            graph.setTileMapping(files_d[tile], tile);
            graph.setTileMapping(starts_d[tile], tile);
            graph.setTileMapping(lengths_d[tile], tile);
            graph.setTileMapping(scratch_d[tile], tile);

            poplar::VertexRef vtx = graph.addVertex(mainCS, "Decoder", {
                {"files", files_d[tile]}, {"pixels", pixels}, 
                {"scratch", scratch_d[tile]}, {"requestBuf", request_d}, 
                {"starts", starts_d[tile]}, {"lengths", lengths_d[tile]},
                {"transitionPixels", transitionPixels}, {"width", blockWidth_d}
            });
            graph.setTileMapping(vtx, tile);
            graph.setPerfEstimate(vtx, 10000000);
        }    
    }
    graph.setTileMapping(request_d, 0);
    graph.setTileMapping(blockWidth_d, 0);
    graph.setInitialValue<unsigned>(blockWidth_d, {slides.block_w});


    poplar::program::Sequence loadProg({
        poplar::program::Copy(filesStream, files_d),
        poplar::program::Copy(startsStream, starts_d),
        poplar::program::Copy(lengthsStream, lengths_d),
    });
    poplar::program::Sequence renderProg({
        poplar::program::Copy(requestStream, request_d),
        poplar::program::Execute(mainCS),
        poplar::program::Copy(pixels_d[0], pixelsStream),
    });

    printf("Creating IPU engine\n");
    poplar::Engine engine(graph, {loadProg, renderProg});
    engine.connectStream("pixels-stream", pixels_h.data());
    engine.connectStream("request-stream", &slides.ipuRequest);
    engine.connectStream("files-stream", slides.files.data());
    engine.connectStream("starts-stream", slides.starts.data());
    engine.connectStream("lengths-stream", slides.lengths.data());
    engine.load(device);
    printf("Uploading slides to IPU\n");
    engine.run(0);



    SDL_Init( SDL_INIT_EVERYTHING );
    SDL_Window* window = SDL_CreateWindow( "SDL", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, windowWidth, windowHeight, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer* renderer = SDL_CreateRenderer( window, -1, SDL_RENDERER_ACCELERATED );
    SDL_SetHint( SDL_HINT_RENDER_SCALE_QUALITY, "1" );
    SDL_Texture* texture = SDL_CreateTexture( renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, slides.width, slides.height);

    bool running = true;
    while( running )
    {
        // handle events
        SDL_Event ev;
        while( SDL_PollEvent( &ev ) )
        {
            if(( SDL_QUIT == ev.type ) || ( SDL_KEYDOWN == ev.type && SDL_SCANCODE_ESCAPE == ev.key.keysym.scancode ) ) {
                running = false;
                break;
            }

            if( SDL_KEYDOWN == ev.type) {
                if (SDL_SCANCODE_LEFT == ev.key.keysym.scancode) slides.prevSlide();
                if (SDL_SCANCODE_RIGHT == ev.key.keysym.scancode) slides.nextSlide();
            }
        }
        
        slides.tick();
        engine.run(1);

        SDL_UpdateTexture( texture, nullptr, pixels_h.data(), slides.width * bytesPerPixel );
        SDL_RenderCopy( renderer, texture, nullptr, nullptr );
        SDL_RenderPresent( renderer );
        
        UpdateFrameTiming();
    }

    SDL_DestroyRenderer( renderer );
    SDL_DestroyWindow( window );
    SDL_Quit();


    return 0;
}