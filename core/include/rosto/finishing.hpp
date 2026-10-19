#pragma once
#include "rosto/temporal.hpp"
#include <cstdint>

namespace rosto {
// Independent, opt-in controls. RetouchParams and its legacy defaults are unchanged.
struct LocalFinishParams {
  float concealer{};     // [0,1], at most +12 encoded RGB levels, highlight gated.
  float lip_coverage{};  // [0,1], both lips, preserves inner mouth and highlights.
};
struct PixelRegion { int x{}, y{}, width{}, height{}; };
struct LocalMask { float concealer{}, lips{}, eye_protection{}, mouth_protection{}; };
struct LocalMaskMap {
  PixelRegion region;
  std::vector<LocalMask> pixels;
};
// Geometry is evaluated in full-frame pixel coordinates. Reference scans the
// full frame with identical arithmetic; ROI skips provably zero mask support.
LocalMaskMap local_masks(const FaceMesh&, int width, int height, bool reference = false);
void process_local(ImageView image, const LocalMaskMap&, const LocalFinishParams&);
void process_local_rgb(uint8_t* rgb, int width, int height, int stride_bytes,
                       const LocalMaskMap&, const LocalFinishParams&);
// Separate experimental path: legacy LocalFinishParams/masks remain unchanged.
// Source protection restores the original eyes, mouth opening and lip detail
// after the single skin pass; the new tint never samples neighboring pixels.
struct RefinedFinishParams {
  float concealer{};
  float lip_chroma{};
  float cheek_lift{};  // [0,1], at most +6 RGB levels, highlight gated.
};
struct RefinedMask {
  float concealer{}, lips{}, eye_protection{}, mouth_protection{}, lip_protection{}, cheeks{};
};
struct RefinedMaskMap {
  PixelRegion region;
  std::vector<RefinedMask> pixels;
};
RefinedMaskMap refined_local_masks(const FaceMesh&, int width, int height,
                                  bool reference = false);
void process_refined_rgb(uint8_t* rgb, const uint8_t* original, int width,
                         int height, int stride_bytes, const RefinedMaskMap&,
                         const RefinedFinishParams&);
// Unsafe frames are always zero. Offline lookahead fades BEFORE known failures;
// no stale mesh is rendered on unsafe frames. Each cut resets the envelope.
std::vector<float> treatment_envelope(const std::vector<uint8_t>& safe,
                                      const std::vector<uint8_t>& cuts,
                                      int fade_frames = 12);
// Split at cuts, never interpolate missing meshes; keep eye/lip contours near
// current measurements (pixel displacement cap), rather than delaying speech.
std::vector<TimedMesh> stabilize_expression_meshes(
    const std::vector<TimedMesh>& input, const std::vector<uint8_t>& cuts,
    int width, int height, float feature_cap_pixels = 1.0F);
}  // namespace rosto
