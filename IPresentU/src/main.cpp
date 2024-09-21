// g++ src/IPresentU.cpp -I /usr/local/include/SDL2 -I/opt/poplar/include -lSDL2 -lpoplar -Wall  -Wextra -O2 -o IPresentU

#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <chrono>

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


int main(int argc, char** argv) {
    if(argc < 2) {
        fprintf(stderr, "USAGE: %s filename.jpg ...\n", argv[0]);
        return EXIT_FAILURE;
    }

    const unsigned int texWidth = 160;
    const unsigned int texHeight = 161;
    const unsigned int bytesPerPixel = 3;
    const unsigned int pixelsSize = texWidth * texHeight * bytesPerPixel;
    const unsigned int filesSize = pixelsSize; // TMP
    const unsigned int scratchSize = 200000;


    // Setup IPU resources
    auto device = getIPU(true);
    poplar::Graph graph(device.getTarget());

    std::vector<unsigned char> pixels_h(pixelsSize, 0);
    DecoderTransfer_t* files_h = (DecoderTransfer_t*) malloc(sizeof(DecoderTransfer_t) + filesSize);
    poplar::Tensor pixels_d = graph.addVariable(poplar::UNSIGNED_CHAR, {pixelsSize}, "pixels");
    poplar::Tensor files_d = graph.addVariable(poplar::UNSIGNED_CHAR, {sizeof(DecoderTransfer_t) + filesSize}, "files");
    poplar::Tensor scratch_d = graph.addVariable(poplar::UNSIGNED_CHAR, {scratchSize}, "scratch");
    graph.setTileMapping(pixels_d, 0);
    graph.setTileMapping(files_d, 0);
    graph.setTileMapping(scratch_d, 0);
    poplar::DataStream pixelsStream = graph.addDeviceToHostFIFO("pixels-stream", poplar::UNSIGNED_CHAR, pixelsSize);
    poplar::DataStream filesStream = graph.addHostToDeviceFIFO("files-stream", poplar::UNSIGNED_CHAR, sizeof(DecoderTransfer_t) + filesSize);

    graph.addCodelets("codelets.gp");
    poplar::ComputeSet mainCS = graph.addComputeSet("mainCS");
    poplar::VertexRef decoder = graph.addVertex(mainCS, "Decoder", {{"files", files_d}, {"pixels", pixels_d}, {"scratch", scratch_d}});
    graph.setTileMapping(decoder, 0);
    graph.setPerfEstimate(decoder, 10000000);

    poplar::program::Sequence program({
        poplar::program::Copy(filesStream, files_d),
        poplar::program::Execute(mainCS),
        poplar::program::Copy(pixels_d, pixelsStream),
    });

    poplar::Engine engine(graph, program);
    engine.connectStream("pixels-stream", pixels_h.data());
    engine.connectStream("files-stream", files_h);
    engine.load(device);


    // Load files
    int count = readFile(argv[1], files_h->buf, filesSize);
    if (-1 == count) {
        fprintf(stderr, "Failed to load file %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    files_h->starts[0] = 0;
    files_h->lengths[0] = count;



    SDL_Init( SDL_INIT_EVERYTHING );
    SDL_Window* window = SDL_CreateWindow( "SDL", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, texWidth, texHeight, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI );
    SDL_Renderer* renderer = SDL_CreateRenderer( window, -1, SDL_RENDERER_ACCELERATED );
    SDL_SetHint( SDL_HINT_RENDER_SCALE_QUALITY, "1" );
    SDL_Texture* texture = SDL_CreateTexture( renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, texWidth, texHeight);

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
        //     snekx = (snekx + (rand() % 3) - 1) % texWidth;
        //     sneky = (sneky + (rand() % 3) - 1) % texHeight;

        //     const unsigned int offset = ( texWidth * sneky * 3 ) + snekx * 3;
        //     pixels[ offset + 2 ] = rand() % 256;        // r
        // }
        
        engine.run(0);


        SDL_UpdateTexture( texture, nullptr, pixels_h.data(), texWidth * bytesPerPixel );
        SDL_RenderCopy( renderer, texture, nullptr, nullptr );
        SDL_RenderPresent( renderer );
        
        UpdateFrameTiming();
    }

    SDL_DestroyRenderer( renderer );
    SDL_DestroyWindow( window );
    SDL_Quit();

    free(files_h);

    return 0;
}