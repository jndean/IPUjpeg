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


struct Slides
{
    public:
    
    unsigned numImgs;
    unsigned width, height;
    unsigned block_w, block_h;
    unsigned blocks_x, blocks_y, numBlocks;
    unsigned fileBufSize;
    std::vector<unsigned char> files;
    std::vector<unsigned> starts;
    std::vector<unsigned> lengths;


    Slides(const char* dir) {
        char filename[200];

        // Load metadata
        snprintf(filename, sizeof(filename), "%s/format.meta", dir);
        std::ifstream input(filename, std::ios::binary);
        std::vector<char> buffer(std::istreambuf_iterator<char>(input), {});

        unsigned* data = (unsigned*) buffer.data();
        numImgs = data[0];
        width = data[1];
        height = data[2];
        block_w = data[3];
        block_h = data[4];
        blocks_x = data[5];
        blocks_y = data[6];
        numBlocks = blocks_x * blocks_y;


        // Load slide chunks
        std::vector<int> filebufSizes(numBlocks, 0);
        for (unsigned y = 0; y < blocks_y; ++y) {
            for (unsigned x = 0; x < blocks_x; ++x) {
                for (unsigned img = 0; img < numImgs; ++img) {
                    snprintf(filename, sizeof(filename), "%s/%d_%d_%d.jpg", dir, img, x, y);
                    struct stat stat_buf;
                    int rc = stat(filename, &stat_buf);
                    if (rc) throw std::invalid_argument(filename);
                    filebufSizes[y * blocks_x + x] += stat_buf.st_size;
                }
            }
        }
        int maxBufSize = *std::max_element(filebufSizes.begin(), filebufSizes.end());
        fileBufSize = (maxBufSize + 3u) & (~3u); // 4-byte pad

        files = std::vector<unsigned char>(numBlocks * fileBufSize);
        starts = std::vector<unsigned>(numBlocks * numImgs);
        lengths = std::vector<unsigned>(numBlocks * numImgs);
        auto start_it = starts.begin();
        auto length_it = lengths.begin();

        for (unsigned y = 0; y < blocks_y; ++y) {
            for (unsigned x = 0; x < blocks_x; ++x) {

                unsigned bufOffset = fileBufSize * (y * blocks_x + x);
                unsigned bufPos = 0;
                for (unsigned img = 0; img < numImgs; ++img) {
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
                }
            }
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
    printf("N=%d, height=%d\n", slides.numImgs, slides.height);


    const unsigned int bytesPerPixel = 3;
    const unsigned int pixelsSize = slides.width * slides.height * bytesPerPixel;
    const unsigned int filesSize = slides.files.size(); // TMP
    const unsigned int numTiles = slides.numBlocks;
    const unsigned int scratchSize = 200000;

    // Setup IPU resources
    auto device = getIPU(true);
    poplar::Graph graph(device.getTarget());

    std::vector<unsigned char> pixels_h(pixelsSize, 0);
    poplar::Tensor pixels_d = graph.addVariable(poplar::UNSIGNED_CHAR, {slides.height, slides.width, bytesPerPixel}, "pixels");
    poplar::Tensor files_d = graph.addVariable(poplar::UNSIGNED_CHAR, {numTiles, slides.fileBufSize}, "files");
    poplar::Tensor starts_d = graph.addVariable(poplar::UNSIGNED_INT, {numTiles, slides.numImgs}, "starts");
    poplar::Tensor lengths_d = graph.addVariable(poplar::UNSIGNED_INT, {numTiles, slides.numImgs}, "lengths");
    poplar::Tensor scratch_d = graph.addVariable(poplar::UNSIGNED_CHAR, {numTiles, scratchSize}, "scratch");
    poplar::DataStream pixelsStream = graph.addDeviceToHostFIFO("pixels-stream", poplar::UNSIGNED_CHAR, pixelsSize);
    poplar::DataStream filesStream = graph.addHostToDeviceFIFO("files-stream", poplar::UNSIGNED_CHAR, filesSize);
    poplar::DataStream startsStream = graph.addHostToDeviceFIFO("starts-stream", poplar::UNSIGNED_INT, slides.starts.size());
    poplar::DataStream lengthsStream = graph.addHostToDeviceFIFO("lengths-stream", poplar::UNSIGNED_INT, slides.lengths.size());

    graph.addCodelets("codelets.gp");
    poplar::ComputeSet mainCS = graph.addComputeSet("mainCS");

    for (unsigned y = 0, tile = 0; y < slides.blocks_y; ++y) {
        for (unsigned x = 0; x < slides.blocks_x; ++x, ++tile) {
            auto block = pixels_d.slice(
                {(y    ) * slides.block_h, (x    ) * slides.block_w},
                {(y + 1) * slides.block_h, (x + 1) * slides.block_w}
            ).flatten();
            graph.setTileMapping(block, tile);
            graph.setTileMapping(files_d[tile], tile);
            graph.setTileMapping(starts_d[tile], tile);
            graph.setTileMapping(lengths_d[tile], tile);
            graph.setTileMapping(scratch_d[tile], tile);

            poplar::VertexRef vtx = graph.addVertex(mainCS, "Decoder", {
                {"files", files_d[tile]}, {"pixels", block}, {"scratch", scratch_d[tile]},
                {"starts", starts_d[tile]}, {"lengths", lengths_d[tile]}
            });
            graph.setTileMapping(vtx, tile);
            graph.setPerfEstimate(vtx, 10000000);
        }    
    }

    poplar::program::Sequence program({
        poplar::program::Copy(filesStream, files_d),
        poplar::program::Copy(startsStream, starts_d),
        poplar::program::Copy(lengthsStream, lengths_d),
        poplar::program::Execute(mainCS),
        poplar::program::Copy(pixels_d, pixelsStream),
    });

    poplar::Engine engine(graph, program);
    engine.connectStream("pixels-stream", pixels_h.data());
    engine.connectStream("files-stream", slides.files.data());
    engine.connectStream("starts-stream", slides.starts.data());
    engine.connectStream("lengths-stream", slides.lengths.data());
    engine.load(device);



    SDL_Init( SDL_INIT_EVERYTHING );
    SDL_Window* window = SDL_CreateWindow( "SDL", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, slides.width, slides.height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI );
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
            if( ( SDL_QUIT == ev.type ) ||
                ( SDL_KEYDOWN == ev.type && SDL_SCANCODE_ESCAPE == ev.key.keysym.scancode ) )
            {
                running = false;
                break;
            }

            // if( SDL_KEYDOWN == ev.type && SDL_SCANCODE_L == ev.key.keysym.scancode )
            // {
            //     useLocktexture = !useLocktexture;
            //     std::cout << "Using " << ( useLocktexture ? "SDL_LockTexture() + std::copy_n()" : "SDL_UpdateTexture()" ) << '\n';
            // }
        }
        
        // splat down some random pixels
        // for( unsigned int i = 0; i < 2; i++ ) {
        //     snekx = (snekx + (rand() % 3) - 1) % slides.width;
        //     sneky = (sneky + (rand() % 3) - 1) % slides.height;

        //     const unsigned int offset = ( slides.width * sneky * 3 ) + snekx * 3;
        //     pixels[ offset + 2 ] = rand() % 256;        // r
        // }
        
        engine.run(0);


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