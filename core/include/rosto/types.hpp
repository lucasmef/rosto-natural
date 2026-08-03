#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace rosto {

struct Color {
  float r{}, g{}, b{}, a{1.0F};
};

struct Point {
  float x{}, y{};
};

struct FaceGeometry {
  Point center{0.5F, 0.48F};
  float width{0.38F};
  float height{0.56F};
  float roll{};
  float confidence{1.0F};
};

struct FaceMesh {
  static constexpr size_t kLandmarkCount = 478;
  std::array<Point, kLandmarkCount> points{};
  float confidence{};

  bool valid() const { return confidence > 0.0F; }
};

struct ImageView {
  float* rgba{};
  int width{};
  int height{};
  int stride_floats{};

  Color get(int x, int y) const {
    const float* p = rgba + y * stride_floats + x * 4;
    return {p[0], p[1], p[2], p[3]};
  }
  void set(int x, int y, Color c) {
    float* p = rgba + y * stride_floats + x * 4;
    p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = c.a;
  }
};

struct OwnedImage {
  int width{}, height{};
  std::vector<float> pixels;

  OwnedImage() = default;
  OwnedImage(int w, int h) : width(w), height(h),
      pixels(static_cast<size_t>(w) * static_cast<size_t>(h) * 4U) {
    if (w <= 0 || h <= 0) throw std::invalid_argument("invalid image dimensions");
  }
  ImageView view() { return {pixels.data(), width, height, width * 4}; }
};

inline float clamp01(float v) { return std::clamp(v, 0.0F, 1.0F); }

}  // namespace rosto
