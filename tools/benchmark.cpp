#include <chrono>
#include <iostream>

#include "rosto/retouch.hpp"

int main(int argc, char** argv) {
  const int width = argc > 1 ? std::stoi(argv[1]) : 3840;
  const int height = argc > 2 ? std::stoi(argv[2]) : 2160;
  rosto::OwnedImage image(width, height);
  for (size_t i = 0; i < image.pixels.size(); i += 4) {
    image.pixels[i] = .58F; image.pixels[i + 1] = .39F;
    image.pixels[i + 2] = .31F; image.pixels[i + 3] = 1.0F;
  }
  const auto start = std::chrono::steady_clock::now();
  rosto::process(image.view(), rosto::FaceGeometry{}, rosto::RetouchParams{});
  const double elapsed = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - start).count();
  std::cout << width << 'x' << height << ": " << elapsed * 1000.0
            << " ms CPU reference\n";
}
