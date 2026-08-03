#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>

#include "rosto/detector.hpp"
#include "rosto_mediapipe_bridge.h"

namespace {
int face_token;
int hair_token;

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
}  // namespace

extern "C" {
void* rosto_mp_face_create(const char*, char*, size_t) {
  return &face_token;
}

void rosto_mp_face_destroy(void* handle) {
  require(handle == &face_token, "invalid face handle");
}

int rosto_mp_face_detect(
    void* handle, const uint8_t* rgba, int width, int height, int row_bytes,
    float* landmarks_xy, size_t landmark_count, float* confidence,
    char*, size_t) {
  require(handle == &face_token, "invalid face handle");
  require(rgba && width == 4 && height == 3 && row_bytes == 16,
          "invalid face input image");
  require(landmark_count == rosto::FaceMesh::kLandmarkCount,
          "invalid face output size");
  for (size_t index = 0; index < landmark_count; ++index) {
    landmarks_xy[index * 2] = static_cast<float>(index) / landmark_count;
    landmarks_xy[index * 2 + 1] = 0.25F;
  }
  *confidence = 0.93F;
  return 1;
}

void* rosto_mp_hair_create(const char*, char*, size_t) {
  return &hair_token;
}

void rosto_mp_hair_destroy(void* handle) {
  require(handle == &hair_token, "invalid hair handle");
}

int rosto_mp_hair_segment(
    void* handle, const uint8_t* rgba, int width, int height, int row_bytes,
    float* probability, size_t count, char*, size_t) {
  require(handle == &hair_token, "invalid hair handle");
  require(rgba && width == 4 && height == 3 && row_bytes == 16,
          "invalid hair input image");
  require(count == 12, "invalid hair output size");
  std::fill(probability, probability + count, 0.73F);
  return 1;
}

const char* rosto_mp_runtime_version(void) {
  return "fake-test-runtime";
}
}

int main() {
  rosto::OwnedImage image(4, 3);
  auto view = image.view();
  for (int y = 0; y < image.height; ++y)
    for (int x = 0; x < image.width; ++x)
      view.set(x, y, {0.1F * x, 0.2F * y, 0.4F, 1.0F});

  {
    auto detector = rosto::make_mediapipe_detector("face.task");
    const auto mesh = detector->detect_mesh(view);
    require(mesh.has_value(), "face mesh was not returned");
    require(mesh->valid(), "face mesh is invalid");
    require(mesh->confidence == 0.93F, "face confidence was not forwarded");
    require(mesh->points[120].y == 0.25F,
            "face landmarks were not forwarded");
  }
  {
    auto segmenter =
        rosto::make_mediapipe_hair_segmenter("hair.tflite");
    const auto mask = segmenter->segment_hair(view);
    require(mask.width == 4 && mask.height == 3,
            "hair mask dimensions are invalid");
    require(mask.values.size() == 12, "hair mask size is invalid");
    require(mask.values[7] == 0.73F,
            "hair probabilities were not forwarded");
  }
  std::cout << "mediapipe_adapter: 1 test passed\n";
}
