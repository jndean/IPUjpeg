#include <algorithm>
#include <stdlib.h>

#include <poplar/DeviceManager.hpp>
#include <poplar/Engine.hpp>
#include <poplar/IPUModel.hpp>

#include "JPGReader.hpp"

poplar::Device getIPU(bool use_hardware = true, int num_ipus = 1);


int main(int argc, char** argv) {
  if (argc != 2) {
    printf("USAGE: %s <jpgfile>\n", argv[0]);
    return EXIT_FAILURE;
  }

  auto ipuDevice = getIPU(false);

  const char* filename = argv[1];
  auto reader = std::make_unique<JPGReader>(ipuDevice, true);
  reader->read(filename);
  reader->decode();
  reader->write("outfile.ppm");

  if (TIMINGSTATS) {
    // Warmup
    for (auto i = 0; i < 20; ++i) {
      reader->read(filename);
      reader->decode();
    }
    reader->timings.clear();
    for (auto i = 0; i < 100; ++i) {
      reader->read(filename);
      reader->decode();
    }
    reader->printTimingStats();
  }

  return EXIT_SUCCESS;
}

poplar::Device getIPU(bool use_hardware, int num_ipus) {

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
