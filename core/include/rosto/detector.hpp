#pragma once

#include <memory>
#include <optional>

#include "rosto/flyaway.hpp"
#include "rosto/types.hpp"

namespace rosto {

class FaceDetector {
 public:
  virtual ~FaceDetector() = default;
  virtual std::optional<FaceMesh> detect_mesh(const ImageView& image) = 0;
};

class HairSegmenter {
 public:
  virtual ~HairSegmenter() = default;
  virtual OwnedMask segment_hair(const ImageView& image) = 0;
};

// Compiled only when MediaPipe Tasks is supplied by the integrator.
std::unique_ptr<FaceDetector> make_mediapipe_detector(const char* model_path);
std::unique_ptr<HairSegmenter> make_mediapipe_hair_segmenter(
    const char* model_path);

}  // namespace rosto
