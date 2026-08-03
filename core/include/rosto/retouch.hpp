#pragma once

#include "rosto/types.hpp"

namespace rosto {

struct RetouchParams {
  float master{0.72F};
  float skin{0.58F};
  float preserve_texture{0.75F};
  float under_eye{0.42F};
  float tone{0.24F};
  float shine{0.30F};
  float eyes{0.18F};
  // Hair analysis is intentionally independent from the facial masks. The
  // native adapter forwards this value to the sparse flyaway stage.
  float flyaways{0.0F};
  float blush{0.0F};
  Color blush_color{0.86F, 0.34F, 0.40F, 1.0F};
  float lips{0.20F};
  Color lip_color{0.62F, 0.16F, 0.20F, 1.0F};
  bool before_after{false};
};

struct MaskSample {
  float face{};
  float skin{};
  float under_eye{};
  float eyes{};
  float brows{};
  float blush{};
  float lips{};
};

MaskSample sample_masks(float nx, float ny, const FaceGeometry& face);
void process(ImageView image, const FaceGeometry& face, const RetouchParams& params);
void process(ImageView image, const FaceMesh& mesh, const RetouchParams& params);

}  // namespace rosto
