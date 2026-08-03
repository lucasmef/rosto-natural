#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "rosto/flyaway.hpp"

namespace {

void write_mask(const std::string& path, rosto::MaskView mask) {
  std::ofstream output(path, std::ios::binary);
  if (!output) throw std::runtime_error("could not create mask output");
  for (int y = 0; y < mask.height; ++y)
    for (int x = 0; x < mask.width; ++x) {
      const unsigned char value = static_cast<unsigned char>(
          rosto::clamp01(mask.get(x, y)) * 255.0F);
      output.write(reinterpret_cast<const char*>(&value), 1);
    }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 7) {
      std::cerr
          << "usage: rosto_flyaway_analysis width height strength "
             "probability.f32 allowed.u8 protected.u8\n";
      return 2;
    }
    const int width = std::stoi(argv[1]);
    const int height = std::stoi(argv[2]);
    const float strength = std::stof(argv[3]);
    rosto::OwnedImage image(width, height);
    rosto::OwnedMask probability(width, height);
    std::ifstream input(argv[4], std::ios::binary);
    if (!input) throw std::runtime_error("could not open probability input");
    input.read(
        reinterpret_cast<char*>(probability.values.data()),
        static_cast<std::streamsize>(
            probability.values.size() * sizeof(float)));
    if (input.gcount() != static_cast<std::streamsize>(
            probability.values.size() * sizeof(float)))
      throw std::runtime_error("probability input has the wrong size");

    rosto::FlyawayAnalysisParams params;
    params.strength = strength;
    const auto analysis =
        rosto::analyze_flyaways(image.view(), probability.view(), params);
    write_mask(argv[5], analysis.allowed_region.view());
    write_mask(argv[6], analysis.protected_hair.view());
    std::cout << "{\"width\":" << width
              << ",\"height\":" << height
              << ",\"strength\":" << strength
              << ",\"allowed_pixels\":" << analysis.allowed_pixels
              << ",\"protected_pixels\":" << analysis.protected_pixels
              << "}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAILED: " << error.what() << '\n';
    return 1;
  }
}
