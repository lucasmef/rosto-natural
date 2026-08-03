#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "rosto/flyaway.hpp"

namespace {

template <typename T>
void read_exact(const std::string& path, std::vector<T>& values) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not open input: " + path);
  input.read(
      reinterpret_cast<char*>(values.data()),
      static_cast<std::streamsize>(values.size() * sizeof(T)));
  if (input.gcount() !=
      static_cast<std::streamsize>(values.size() * sizeof(T)))
    throw std::runtime_error("input has the wrong size: " + path);
}

template <typename T>
void write_exact(const std::string& path, const std::vector<T>& values) {
  std::ofstream output(path, std::ios::binary);
  if (!output) throw std::runtime_error("could not create output: " + path);
  output.write(
      reinterpret_cast<const char*>(values.data()),
      static_cast<std::streamsize>(values.size() * sizeof(T)));
}

void write_mask(const std::string& path, rosto::MaskView mask) {
  std::vector<unsigned char> bytes(
      static_cast<size_t>(mask.width) * mask.height);
  for (int y = 0; y < mask.height; ++y)
    for (int x = 0; x < mask.width; ++x)
      bytes[static_cast<size_t>(y) * mask.width + x] =
          static_cast<unsigned char>(
              rosto::clamp01(mask.get(x, y)) * 255.0F);
  write_exact(path, bytes);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 11) {
      std::cerr
          << "usage: rosto_flyaway_render width height analysis_width "
             "analysis_height strength image.rgba.f32 probability.f32 "
             "output.rgba.f32 allowed.u8 protected.u8\n";
      return 2;
    }
    const int width = std::stoi(argv[1]);
    const int height = std::stoi(argv[2]);
    const int analysis_width = std::stoi(argv[3]);
    const int analysis_height = std::stoi(argv[4]);
    const float strength = std::stof(argv[5]);
    rosto::OwnedImage image(width, height);
    rosto::OwnedImage preview(analysis_width, analysis_height);
    rosto::OwnedMask probability(analysis_width, analysis_height);
    read_exact(argv[6], image.pixels);
    read_exact(argv[7], probability.values);

    rosto::FlyawayAnalysisParams analysis_params;
    analysis_params.strength = strength;
    const auto analysis = rosto::analyze_flyaways(
        preview.view(), probability.view(), analysis_params);
    const auto allowed =
        rosto::resize_mask(analysis.allowed_region.view(), width, height);
    const auto protected_hair =
        rosto::resize_mask(analysis.protected_hair.view(), width, height);
    rosto::GuidedFlyawayParams reconstruction;
    reconstruction.strength = strength;
    const auto stats = rosto::reconstruct_guided_flyaways(
        image.view(), allowed.view(), protected_hair.view(), reconstruction);
    write_exact(argv[8], image.pixels);
    write_mask(argv[9], allowed.view());
    write_mask(argv[10], protected_hair.view());
    std::cout << "{\"candidate_pixels\":" << stats.candidate_pixels
              << ",\"reconstructed_pixels\":"
              << stats.reconstructed_pixels
              << ",\"protected_pixels_skipped\":"
              << stats.protected_pixels_skipped << "}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAILED: " << error.what() << '\n';
    return 1;
  }
}
