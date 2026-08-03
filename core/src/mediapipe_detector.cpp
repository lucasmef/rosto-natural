#include "rosto/detector.hpp"

#if !defined(ROSTO_WITH_MEDIAPIPE)
#error "Compile this file only with ROSTO_WITH_MEDIAPIPE"
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "rosto_mediapipe_bridge.h"

namespace rosto {
namespace {
std::vector<uint8_t> task_pixels(const ImageView& image) {
  std::vector<uint8_t> pixels(
      static_cast<size_t>(image.width) * image.height * 4);
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      const Color c = image.get(x, y);
      const size_t offset =
          (static_cast<size_t>(y) * image.width + x) * 4;
      pixels[offset] = static_cast<uint8_t>(clamp01(c.r) * 255.0F);
      pixels[offset + 1] = static_cast<uint8_t>(clamp01(c.g) * 255.0F);
      pixels[offset + 2] = static_cast<uint8_t>(clamp01(c.b) * 255.0F);
      pixels[offset + 3] = 255;
    }
  }
  return pixels;
}

std::runtime_error bridge_error(
    const char* operation, const std::array<char, 1024>& message) {
  return std::runtime_error(
      std::string(operation) + ": " +
      (message[0] ? message.data() : "unknown MediaPipe error"));
}

class MediaPipeDetector final : public FaceDetector {
 public:
  explicit MediaPipeDetector(const char* model_path) {
    std::array<char, 1024> error{};
    handle_ = rosto_mp_face_create(model_path, error.data(), error.size());
    if (!handle_) throw bridge_error("Face Landmarker initialization", error);
  }

  ~MediaPipeDetector() override {
    rosto_mp_face_destroy(handle_);
  }

  std::optional<FaceMesh> detect_mesh(const ImageView& image) override {
    const std::vector<uint8_t> pixels = task_pixels(image);
    std::array<float, FaceMesh::kLandmarkCount * 2> landmarks{};
    std::array<char, 1024> error{};
    float confidence = 0.0F;
    const int result = rosto_mp_face_detect(
        handle_, pixels.data(), image.width, image.height, image.width * 4,
        landmarks.data(), FaceMesh::kLandmarkCount, &confidence,
        error.data(), error.size());
    if (result < 0) throw bridge_error("Face Landmarker inference", error);
    if (result == 0) return std::nullopt;
    FaceMesh mesh;
    mesh.confidence = confidence;
    for (size_t index = 0; index < FaceMesh::kLandmarkCount; ++index)
      mesh.points[index] = {
          landmarks[index * 2], landmarks[index * 2 + 1]};
    return mesh;
  }

 private:
  void* handle_{};
};

class MediaPipeHairSegmenter final : public HairSegmenter {
 public:
  explicit MediaPipeHairSegmenter(const char* model_path) {
    std::array<char, 1024> error{};
    handle_ = rosto_mp_hair_create(model_path, error.data(), error.size());
    if (!handle_) throw bridge_error("Hair Segmenter initialization", error);
  }

  ~MediaPipeHairSegmenter() override {
    rosto_mp_hair_destroy(handle_);
  }

  OwnedMask segment_hair(const ImageView& image) override {
    const std::vector<uint8_t> pixels = task_pixels(image);
    OwnedMask probability(image.width, image.height);
    std::array<char, 1024> error{};
    const int result = rosto_mp_hair_segment(
        handle_, pixels.data(), image.width, image.height, image.width * 4,
        probability.values.data(), probability.values.size(),
        error.data(), error.size());
    if (result < 0) throw bridge_error("Hair Segmenter inference", error);
    return probability;
  }

 private:
  void* handle_{};
};
}  // namespace

std::unique_ptr<FaceDetector> make_mediapipe_detector(const char* model_path) {
  return std::make_unique<MediaPipeDetector>(model_path);
}

std::unique_ptr<HairSegmenter> make_mediapipe_hair_segmenter(
    const char* model_path) {
  return std::make_unique<MediaPipeHairSegmenter>(model_path);
}
}  // namespace rosto
