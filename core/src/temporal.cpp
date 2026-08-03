#include "rosto/temporal.hpp"

#include <algorithm>
#include <cmath>

namespace rosto {
namespace {
float median(std::vector<float> values) {
  std::sort(values.begin(), values.end());
  return values[values.size() / 2];
}

float point_distance(Point a, Point b) {
  return std::hypot(a.x - b.x, a.y - b.y);
}

float smoothstep(float a, float b, float x) {
  const float t = std::clamp((x - a) / (b - a), 0.0F, 1.0F);
  return t * t * (3.0F - 2.0F * t);
}

float base_alpha(double dt, const TemporalOptions& options) {
  return static_cast<float>(1.0 - std::exp2(
      -dt / std::max(0.001, options.half_life_seconds)));
}

float adaptive_alpha(double dt, float motion,
                     const TemporalOptions& options) {
  const float base = base_alpha(dt, options);
  // A static face gets the full 80 ms denoising. Intentional translation,
  // scale or rotation progressively releases the filter to prevent mask lag.
  const float release = smoothstep(.0015F, .020F, motion);
  return base + (.92F - base) * release;
}

float face_motion(const FaceGeometry& a, const FaceGeometry& b) {
  const float scale = std::max((a.width + b.width) * .5F, 1e-5F);
  return point_distance(a.center, b.center) / scale +
      std::abs(a.width - b.width) / scale * .50F +
      std::abs(a.height - b.height) /
          std::max((a.height + b.height) * .5F, 1e-5F) * .35F +
      std::abs(a.roll - b.roll) * .25F;
}

FaceGeometry lerp(const FaceGeometry& a, const FaceGeometry& b, float t) {
  return {{a.center.x + (b.center.x - a.center.x) * t,
           a.center.y + (b.center.y - a.center.y) * t},
          a.width + (b.width - a.width) * t,
          a.height + (b.height - a.height) * t,
          a.roll + (b.roll - a.roll) * t,
          a.confidence + (b.confidence - a.confidence) * t};
}

FaceGeometry median_face(const std::vector<TimedFace>& v, int index, int radius) {
  std::vector<float> x, y, w, h, r, c;
  const int first = std::max(0, index - radius);
  const int last = std::min(static_cast<int>(v.size()) - 1, index + radius);
  for (int i = first; i <= last; ++i) {
    if (!v[i].face) continue;
    x.push_back(v[i].face->center.x); y.push_back(v[i].face->center.y);
    w.push_back(v[i].face->width); h.push_back(v[i].face->height);
    r.push_back(v[i].face->roll); c.push_back(v[i].face->confidence);
  }
  if (x.empty()) return {};
  return {{median(x), median(y)}, median(w), median(h), median(r), median(c)};
}

FaceMesh lerp_mesh(const FaceMesh& a, const FaceMesh& b, float t) {
  FaceMesh out;
  for (size_t i = 0; i < FaceMesh::kLandmarkCount; ++i) {
    out.points[i].x = a.points[i].x + (b.points[i].x - a.points[i].x) * t;
    out.points[i].y = a.points[i].y + (b.points[i].y - a.points[i].y) * t;
  }
  out.confidence = a.confidence + (b.confidence - a.confidence) * t;
  return out;
}

FaceMesh median_mesh(const std::vector<TimedMesh>& v, int index, int radius) {
  FaceMesh out;
  const int first = std::max(0, index - radius);
  const int last = std::min(static_cast<int>(v.size()) - 1, index + radius);
  for (size_t point = 0; point < FaceMesh::kLandmarkCount; ++point) {
    std::vector<float> x, y;
    for (int i = first; i <= last; ++i) {
      if (!v[i].mesh) continue;
      x.push_back(v[i].mesh->points[point].x);
      y.push_back(v[i].mesh->points[point].y);
    }
    if (!x.empty()) out.points[point] = {median(x), median(y)};
  }
  std::vector<float> confidence;
  for (int i = first; i <= last; ++i)
    if (v[i].mesh) confidence.push_back(v[i].mesh->confidence);
  out.confidence = confidence.empty() ? 0.0F : median(confidence);
  return out;
}

struct MeshPose {
  Point center{};
  float width{};
  float roll{};
};

MeshPose mesh_pose(const FaceMesh& mesh) {
  const Point left = mesh.points[234], right = mesh.points[454];
  const Point eye_left = mesh.points[33], eye_right = mesh.points[263];
  return {{(left.x + right.x) * .5F, (left.y + right.y) * .5F},
          std::max(point_distance(left, right), 1e-5F),
          std::atan2(eye_right.y - eye_left.y, eye_right.x - eye_left.x)};
}

float mesh_motion(const FaceMesh& a, const FaceMesh& b) {
  const MeshPose pa = mesh_pose(a), pb = mesh_pose(b);
  const float scale = std::max((pa.width + pb.width) * .5F, 1e-5F);
  return point_distance(pa.center, pb.center) / scale +
      std::abs(pa.width - pb.width) / scale * .50F +
      std::abs(pa.roll - pb.roll) * .25F;
}
}  // namespace

std::vector<TimedFace> stabilize_faces(const std::vector<TimedFace>& input,
                                       const TemporalOptions& options) {
  if (input.empty()) return {};
  std::vector<TimedFace> out = input;
  for (size_t i = 0; i < out.size();) {
    if (out[i].face) { ++i; continue; }
    const size_t begin = i;
    while (i < out.size() && !out[i].face) ++i;
    const size_t gap = i - begin;
    if (begin > 0 && i < out.size() &&
        gap <= static_cast<size_t>(options.max_gap_frames)) {
      for (size_t k = 0; k < gap; ++k) {
        const float t = static_cast<float>(k + 1) / static_cast<float>(gap + 1);
        out[begin + k].face = lerp(*out[begin - 1].face, *out[i].face, t);
      }
    }
  }

  std::vector<TimedFace> med = out;
  for (size_t i = 0; i < out.size(); ++i)
    if (out[i].face) med[i].face = median_face(out, static_cast<int>(i),
                                               options.median_radius);

  const std::vector<TimedFace> motion_reference = med;
  auto pass = [&](bool forward) {
    std::optional<FaceGeometry> state;
    const int n = static_cast<int>(med.size());
    for (int k = 0; k < n; ++k) {
      const int i = forward ? k : n - 1 - k;
      if (!med[i].face) { state.reset(); continue; }
      if (!state) { state = med[i].face; continue; }
      const int prev = forward ? i - 1 : i + 1;
      const double dt = std::abs(med[i].seconds - med[prev].seconds);
      const float alpha = adaptive_alpha(
          dt, face_motion(*motion_reference[i].face,
                          *motion_reference[prev].face), options);
      state = lerp(*state, *med[i].face, alpha);
      med[i].face = state;
    }
  };
  pass(true);
  pass(false);
  return med;
}

std::vector<TimedMesh> stabilize_meshes(const std::vector<TimedMesh>& input,
                                        const TemporalOptions& options) {
  if (input.empty()) return {};
  std::vector<TimedMesh> out = input;
  for (size_t i = 0; i < out.size();) {
    if (out[i].mesh) { ++i; continue; }
    const size_t begin = i;
    while (i < out.size() && !out[i].mesh) ++i;
    const size_t gap = i - begin;
    if (begin > 0 && i < out.size() &&
        gap <= static_cast<size_t>(options.max_gap_frames)) {
      for (size_t k = 0; k < gap; ++k) {
        const float t = static_cast<float>(k + 1) / static_cast<float>(gap + 1);
        out[begin + k].mesh = lerp_mesh(*out[begin - 1].mesh, *out[i].mesh, t);
      }
    }
  }
  std::vector<TimedMesh> med = out;
  for (size_t i = 0; i < out.size(); ++i)
    if (out[i].mesh) med[i].mesh = median_mesh(out, static_cast<int>(i),
                                               options.median_radius);
  const std::vector<TimedMesh> motion_reference = med;
  auto pass = [&](bool forward) {
    std::optional<FaceMesh> state;
    const int n = static_cast<int>(med.size());
    for (int k = 0; k < n; ++k) {
      const int i = forward ? k : n - 1 - k;
      if (!med[i].mesh) { state.reset(); continue; }
      if (!state) { state = med[i].mesh; continue; }
      const int prev = forward ? i - 1 : i + 1;
      const double dt = std::abs(med[i].seconds - med[prev].seconds);
      const float alpha = adaptive_alpha(
          dt, mesh_motion(*motion_reference[i].mesh,
                          *motion_reference[prev].mesh), options);
      state = lerp_mesh(*state, *med[i].mesh, alpha);
      med[i].mesh = state;
    }
  };
  pass(true);
  pass(false);
  return med;
}

}  // namespace rosto
