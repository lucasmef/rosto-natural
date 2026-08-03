#include "rosto/flyaway.hpp"

#include <algorithm>
#include <cmath>

namespace rosto {
namespace {

bool compatible(const ImageView& image, const MaskView& mask) {
  return mask.values && mask.width == image.width &&
         mask.height == image.height && mask.stride_floats >= mask.width;
}

Color mix(Color a, Color b, float t) {
  return {a.r + (b.r - a.r) * t,
          a.g + (b.g - a.g) * t,
          a.b + (b.b - a.b) * t,
          a.a};
}

OwnedMask box_average(MaskView source, int radius) {
  OwnedMask result(source.width, source.height);
  const int stride = source.width + 1;
  std::vector<double> integral(
      static_cast<size_t>(source.width + 1) *
      static_cast<size_t>(source.height + 1), 0.0);
  for (int y = 0; y < source.height; ++y) {
    double row = 0.0;
    for (int x = 0; x < source.width; ++x) {
      row += clamp01(source.get(x, y));
      integral[static_cast<size_t>(y + 1) * stride + x + 1] =
          integral[static_cast<size_t>(y) * stride + x + 1] + row;
    }
  }
  for (int y = 0; y < source.height; ++y) {
    const int top = std::max(0, y - radius);
    const int bottom = std::min(source.height - 1, y + radius);
    for (int x = 0; x < source.width; ++x) {
      const int left = std::max(0, x - radius);
      const int right = std::min(source.width - 1, x + radius);
      const double sum =
          integral[static_cast<size_t>(bottom + 1) * stride + right + 1] -
          integral[static_cast<size_t>(top) * stride + right + 1] -
          integral[static_cast<size_t>(bottom + 1) * stride + left] +
          integral[static_cast<size_t>(top) * stride + left];
      const int area = (right - left + 1) * (bottom - top + 1);
      result.at(x, y) = static_cast<float>(sum / std::max(1, area));
    }
  }
  return result;
}

OwnedMask dilate_square(MaskView source, int radius) {
  OwnedMask horizontal(source.width, source.height);
  OwnedMask result(source.width, source.height);
  for (int y = 0; y < source.height; ++y)
    for (int x = 0; x < source.width; ++x) {
      float maximum = 0.0F;
      for (int dx = -radius; dx <= radius; ++dx)
        maximum = std::max(maximum, clamp01(source.get(
            std::clamp(x + dx, 0, source.width - 1), y)));
      horizontal.at(x, y) = maximum;
    }
  for (int y = 0; y < source.height; ++y)
    for (int x = 0; x < source.width; ++x) {
      float maximum = 0.0F;
      for (int dy = -radius; dy <= radius; ++dy)
        maximum = std::max(maximum, horizontal.view().get(
            x, std::clamp(y + dy, 0, source.height - 1)));
      result.at(x, y) = maximum;
    }
  return result;
}

}  // namespace

FlyawayAnalysis analyze_flyaways(
    ImageView image,
    MaskView hair_probability,
    const FlyawayAnalysisParams& params) {
  if (!compatible(image, hair_probability))
    throw std::invalid_argument(
        "hair probability must match the analysis image");

  const float strength = clamp01(params.strength);
  FlyawayAnalysis analysis{
      OwnedMask(image.width, image.height),
      OwnedMask(image.width, image.height)};
  if (strength <= 0.0F) return analysis;

  const int short_side = std::min(image.width, image.height);
  OwnedMask strong(image.width, image.height);
  for (int y = 0; y < image.height; ++y)
    for (int x = 0; x < image.width; ++x)
      strong.at(x, y) =
          hair_probability.get(x, y) >= params.strong_hair_threshold ?
          1.0F : 0.0F;

  // A local density test removes thin protrusions from the protected mass
  // while retaining the normal hairstyle body.
  const int density_radius =
      std::max(1, static_cast<int>(std::round(short_side * .012F)));
  const OwnedMask density = box_average(strong.view(), density_radius);
  OwnedMask protected_core(image.width, image.height);
  for (int y = 0; y < image.height; ++y)
    for (int x = 0; x < image.width; ++x)
      protected_core.at(x, y) =
          density.view().get(x, y) >= params.protection_density_threshold ?
          1.0F : 0.0F;

  const float padding_ratio =
      params.low_protection_padding_ratio +
      (params.high_protection_padding_ratio -
       params.low_protection_padding_ratio) * strength;
  const int padding =
      std::max(1, static_cast<int>(std::round(short_side * padding_ratio)));
  analysis.protected_hair =
      dilate_square(protected_core.view(), padding);

  std::vector<int> row_left(image.height, image.width);
  std::vector<int> row_right(image.height, -1);
  std::vector<int> column_top(image.width, image.height);
  for (int y = 0; y < image.height; ++y)
    for (int x = 0; x < image.width; ++x)
      if (analysis.protected_hair.view().get(x, y) >= .5F) {
        row_left[y] = std::min(row_left[y], x);
        row_right[y] = std::max(row_right[y], x);
        column_top[x] = std::min(column_top[x], y);
        ++analysis.protected_pixels;
      }

  const float fringe_threshold =
      params.low_fringe_threshold +
      (params.high_fringe_threshold - params.low_fringe_threshold) *
          strength;
  const float distance_ratio =
      params.low_search_distance_ratio +
      (params.high_search_distance_ratio -
       params.low_search_distance_ratio) * strength;
  const int max_distance = std::max(
      4, static_cast<int>(std::round(short_side * distance_ratio)));

  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      if (analysis.protected_hair.view().get(x, y) >= .5F) continue;
      int distance = max_distance + 1;
      bool exterior = false;
      if (row_right[y] >= 0 && x < row_left[y]) {
        exterior = true;
        distance = std::min(distance, row_left[y] - x);
      }
      if (row_right[y] >= 0 && x > row_right[y]) {
        exterior = true;
        distance = std::min(distance, x - row_right[y]);
      }
      if (column_top[x] < image.height && y < column_top[x]) {
        exterior = true;
        distance = std::min(distance, column_top[x] - y);
      }
      if (!exterior || distance > max_distance) continue;
      const float semantic = clamp01(hair_probability.get(x, y));
      if (semantic < fringe_threshold) continue;
      analysis.allowed_region.at(x, y) = 1.0F;
      ++analysis.allowed_pixels;
    }
  }
  return analysis;
}

OwnedMask resize_mask(MaskView source, int width, int height) {
  if (!source.values || source.width <= 0 || source.height <= 0 ||
      width <= 0 || height <= 0)
    throw std::invalid_argument("invalid mask resize");
  OwnedMask result(width, height);
  const float scale_x = static_cast<float>(source.width) / width;
  const float scale_y = static_cast<float>(source.height) / height;
  for (int y = 0; y < height; ++y) {
    const float source_y =
        std::clamp((y + .5F) * scale_y - .5F, 0.0F,
                   static_cast<float>(source.height - 1));
    const int y0 = static_cast<int>(std::floor(source_y));
    const int y1 = std::min(source.height - 1, y0 + 1);
    const float fy = source_y - y0;
    for (int x = 0; x < width; ++x) {
      const float source_x =
          std::clamp((x + .5F) * scale_x - .5F, 0.0F,
                     static_cast<float>(source.width - 1));
      const int x0 = static_cast<int>(std::floor(source_x));
      const int x1 = std::min(source.width - 1, x0 + 1);
      const float fx = source_x - x0;
      const float top =
          source.get(x0, y0) +
          (source.get(x1, y0) - source.get(x0, y0)) * fx;
      const float bottom =
          source.get(x0, y1) +
          (source.get(x1, y1) - source.get(x0, y1)) * fx;
      result.at(x, y) = clamp01(top + (bottom - top) * fy);
    }
  }
  return result;
}

OwnedMask stabilize_flyaway_mask(
    MaskView previous,
    MaskView current,
    float normalized_motion,
    const FlyawayTemporalParams& params) {
  if (!previous.values || !current.values ||
      previous.width <= 0 || previous.height <= 0 ||
      previous.width != current.width ||
      previous.height != current.height)
    throw std::invalid_argument("temporal flyaway masks must match");
  OwnedMask stable(current.width, current.height);
  const float motion = clamp01(normalized_motion);
  const float attack = clamp01(
      params.attack + (1.0F - params.attack) * motion * .55F);
  const float release = clamp01(
      params.release +
      (params.motion_release - params.release) * motion);
  for (int y = 0; y < current.height; ++y) {
    for (int x = 0; x < current.width; ++x) {
      const float before = clamp01(previous.get(x, y));
      const float now = clamp01(current.get(x, y));
      const float alpha = now >= before ? attack : release;
      stable.at(x, y) = before + (now - before) * alpha;
    }
  }
  return stable;
}

FlyawayStats reconstruct_flyaways(
    ImageView image,
    MaskView flyaway_mask,
    MaskView protected_hair,
    const FlyawayParams& params) {
  FlyawayStats stats;
  if (!image.rgba || image.width <= 0 || image.height <= 0 ||
      params.strength <= 0.0F)
    return stats;
  if (!compatible(image, flyaway_mask) ||
      !compatible(image, protected_hair))
    throw std::invalid_argument("flyaway masks must match the image");

  OwnedImage source(image.width, image.height);
  auto source_view = source.view();
  double center_x = 0.0;
  double center_y = 0.0;
  double protected_weight = 0.0;
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      source_view.set(x, y, image.get(x, y));
      const float weight = clamp01(protected_hair.get(x, y));
      center_x += x * weight;
      center_y += y * weight;
      protected_weight += weight;
    }
  }
  if (protected_weight > 0.0) {
    center_x /= protected_weight;
    center_y /= protected_weight;
  } else {
    center_x = (image.width - 1) * 0.5;
    center_y = (image.height - 1) * 0.5;
  }

  const int max_distance = std::max(
      4, static_cast<int>(std::round(
             std::min(image.width, image.height) *
             std::max(0.005F, params.max_sample_distance_ratio))));
  const float strength = clamp01(params.strength);
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      const float mask = clamp01(flyaway_mask.get(x, y));
      if (mask < params.mask_threshold) continue;
      ++stats.candidate_pixels;
      if (protected_hair.get(x, y) >= params.protected_threshold) {
        ++stats.protected_pixels_skipped;
        continue;
      }

      float dx = static_cast<float>(x - center_x);
      float dy = static_cast<float>(y - center_y);
      const float length = std::sqrt(dx * dx + dy * dy);
      if (length < 1e-4F) continue;
      dx /= length;
      dy /= length;
      const float tangent_x = -dy;
      const float tangent_y = dx;

      Color sum{};
      int samples = 0;
      for (int distance = 3; distance <= max_distance && samples < 3;
           ++distance) {
        const int sx = std::clamp(
            static_cast<int>(std::round(x + dx * distance)),
            0, image.width - 1);
        const int sy = std::clamp(
            static_cast<int>(std::round(y + dy * distance)),
            0, image.height - 1);
        if (flyaway_mask.get(sx, sy) >= params.mask_threshold ||
            protected_hair.get(sx, sy) >= params.protected_threshold)
          continue;

        for (int tangent = -1; tangent <= 1; ++tangent) {
          const int tx = std::clamp(
              static_cast<int>(std::round(sx + tangent_x * tangent)),
              0, image.width - 1);
          const int ty = std::clamp(
              static_cast<int>(std::round(sy + tangent_y * tangent)),
              0, image.height - 1);
          if (flyaway_mask.get(tx, ty) >= params.mask_threshold ||
              protected_hair.get(tx, ty) >= params.protected_threshold)
            continue;
          const Color sample = source_view.get(tx, ty);
          sum.r += sample.r;
          sum.g += sample.g;
          sum.b += sample.b;
          ++samples;
        }
      }
      if (!samples) continue;

      const float inverse = 1.0F / samples;
      const Color background{
          sum.r * inverse, sum.g * inverse, sum.b * inverse, 1.0F};
      image.set(x, y, mix(source_view.get(x, y), background,
                          strength * mask));
      ++stats.reconstructed_pixels;
    }
  }
  return stats;
}

FlyawayStats reconstruct_guided_flyaways(
    ImageView image,
    MaskView allowed_region,
    MaskView protected_hair,
    const GuidedFlyawayParams& params) {
  FlyawayStats stats;
  if (!image.rgba || image.width <= 0 || image.height <= 0 ||
      params.strength <= 0.0F)
    return stats;
  if (!compatible(image, allowed_region) ||
      !compatible(image, protected_hair))
    throw std::invalid_argument("guided flyaway masks must match the image");

  OwnedImage source(image.width, image.height);
  OwnedImage dilated(image.width, image.height);
  OwnedImage closed(image.width, image.height);
  auto source_view = source.view();
  auto dilated_view = dilated.view();
  auto closed_view = closed.view();
  for (int y = 0; y < image.height; ++y)
    for (int x = 0; x < image.width; ++x)
      source_view.set(x, y, image.get(x, y));

  const float strength = clamp01(params.strength);
  const float vertical_extent_ratio =
      params.low_vertical_extent_ratio +
      (params.vertical_extent_ratio - params.low_vertical_extent_ratio) *
          strength;
  const float dark_interruption_threshold =
      params.low_dark_interruption_threshold +
      (params.dark_interruption_threshold -
       params.low_dark_interruption_threshold) * strength;
  const float blend_gain = std::min(
      1.0F, strength / std::max(0.01F, params.full_blend_strength));

  int extent = std::max(
      3, static_cast<int>(std::round(
             std::min(image.width, image.height) *
             std::max(.01F, vertical_extent_ratio))));
  if (extent % 2 == 0) ++extent;
  const int radius = extent / 2;

  // Vertical morphological close: dilation followed by erosion. This keeps
  // long vertical background folds but fills shorter dark interruptions.
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      Color maximum{};
      maximum.r = maximum.g = maximum.b = 0.0F;
      maximum.a = source_view.get(x, y).a;
      for (int offset = -radius; offset <= radius; ++offset) {
        const Color sample = source_view.get(
            x, std::clamp(y + offset, 0, image.height - 1));
        maximum.r = std::max(maximum.r, sample.r);
        maximum.g = std::max(maximum.g, sample.g);
        maximum.b = std::max(maximum.b, sample.b);
      }
      dilated_view.set(x, y, maximum);
    }
  }
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      Color minimum{1.0F, 1.0F, 1.0F, source_view.get(x, y).a};
      for (int offset = -radius; offset <= radius; ++offset) {
        const Color sample = dilated_view.get(
            x, std::clamp(y + offset, 0, image.height - 1));
        minimum.r = std::min(minimum.r, sample.r);
        minimum.g = std::min(minimum.g, sample.g);
        minimum.b = std::min(minimum.b, sample.b);
      }
      closed_view.set(x, y, minimum);
    }
  }

  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      const float allowed = clamp01(allowed_region.get(x, y));
      if (allowed <= 0.0F) continue;
      ++stats.candidate_pixels;
      if (protected_hair.get(x, y) >= params.protected_threshold) {
        ++stats.protected_pixels_skipped;
        continue;
      }
      const Color before = source_view.get(x, y);
      const Color background = closed_view.get(x, y);
      const float dark_interruption =
          0.2126F * (background.r - before.r) +
          0.7152F * (background.g - before.g) +
          0.0722F * (background.b - before.b);
      if (dark_interruption < dark_interruption_threshold) continue;
      image.set(x, y, mix(before, background, allowed * blend_gain));
      ++stats.reconstructed_pixels;
    }
  }
  return stats;
}

}  // namespace rosto
