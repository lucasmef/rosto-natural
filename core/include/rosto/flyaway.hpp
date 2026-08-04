#pragma once

#include <stdexcept>
#include <vector>

#include "rosto/types.hpp"

namespace rosto {

struct MaskView {
  const float* values{};
  int width{};
  int height{};
  int stride_floats{};

  float get(int x, int y) const {
    return values[y * stride_floats + x];
  }
};

struct OwnedMask {
  int width{};
  int height{};
  std::vector<float> values;

  OwnedMask() = default;
  OwnedMask(int w, int h)
      : width(w), height(h),
        values(static_cast<size_t>(w) * static_cast<size_t>(h)) {
    if (w <= 0 || h <= 0)
      throw std::invalid_argument("invalid mask dimensions");
  }

  MaskView view() const { return {values.data(), width, height, width}; }
  float& at(int x, int y) { return values[y * width + x]; }
};

struct FlyawayParams {
  float strength{0.0F};
  float mask_threshold{0.08F};
  float protected_threshold{0.45F};
  float max_sample_distance_ratio{0.035F};
};

struct FlyawayStats {
  int candidate_pixels{};
  int reconstructed_pixels{};
  int protected_pixels_skipped{};
};

struct FlyawayAnalysisParams {
  float strength{0.70F};
  float strong_hair_threshold{0.54F};
  float protection_density_threshold{0.40F};
  float low_fringe_threshold{0.018F};
  float high_fringe_threshold{0.0025F};
  float low_search_distance_ratio{0.080F};
  float high_search_distance_ratio{0.170F};
  float low_protection_padding_ratio{0.010F};
  float high_protection_padding_ratio{0.004F};
};

struct FlyawayAnalysis {
  OwnedMask allowed_region;
  OwnedMask protected_hair;
  int allowed_pixels{};
  int protected_pixels{};
};

struct GuidedFlyawayParams {
  float strength{1.0F};
  float low_vertical_extent_ratio{0.120F};
  float vertical_extent_ratio{0.170F};
  float low_dark_interruption_threshold{5.0F / 255.0F};
  float dark_interruption_threshold{1.0F / 255.0F};
  float full_blend_strength{0.70F};
  float protected_threshold{0.45F};
};

struct FlyawayTemporalParams {
  float attack{0.72F};
  float release{0.24F};
  float motion_release{0.82F};
};

// Converts the HairSegmenter confidence output into two masks at analysis
// resolution. The protected mask contains the hairstyle mass; allowed_region
// contains only semantically hair-like pixels outside its top/side silhouette.
FlyawayAnalysis analyze_flyaways(
    ImageView image,
    MaskView hair_probability,
    const FlyawayAnalysisParams& params = {});

// Removes the complete face oval from the authorized reconstruction region.
// Hair segmentation occasionally produces low-confidence false positives on
// brows or skin; this conservative guard makes those pixels immutable.
void protect_face_from_flyaways(
    FlyawayAnalysis& analysis,
    const FaceMesh& mesh,
    float padding = 0.08F);

// Bilinear resizing keeps analysis masks feathered when a 640 px inference is
// promoted to the full Premiere render resolution.
OwnedMask resize_mask(MaskView source, int width, int height);

// One temporal update for a probability mask. Missing single-frame strands
// decay slowly, while real motion increases release to avoid trails.
OwnedMask stabilize_flyaway_mask(
    MaskView previous,
    MaskView current,
    float normalized_motion,
    const FlyawayTemporalParams& params = {});

// Reconstructs only pixels selected by flyaway_mask. Hair segmentation and
// candidate detection are separate analysis stages so they may run at 512 px
// while this sparse reconstruction retains full output resolution.
FlyawayStats reconstruct_flyaways(
    ImageView image,
    MaskView flyaway_mask,
    MaskView protected_hair,
    const FlyawayParams& params);

// Reconstructs a user-authorized background region while preserving vertical
// structures. Short/curved dark interruptions disappear; persistent folds and
// the protected hairstyle remain.
FlyawayStats reconstruct_guided_flyaways(
    ImageView image,
    MaskView allowed_region,
    MaskView protected_hair,
    const GuidedFlyawayParams& params = {});

}  // namespace rosto
