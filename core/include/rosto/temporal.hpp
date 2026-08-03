#pragma once

#include <optional>
#include <vector>

#include "rosto/types.hpp"

namespace rosto {

struct TimedFace {
  double seconds{};
  std::optional<FaceGeometry> face;
};

struct TimedMesh {
  double seconds{};
  std::optional<FaceMesh> mesh;
};

struct TemporalOptions {
  int median_radius{2};
  double half_life_seconds{0.080};
  int max_gap_frames{8};
};

std::vector<TimedFace> stabilize_faces(const std::vector<TimedFace>& input,
                                       const TemporalOptions& options = {});
std::vector<TimedMesh> stabilize_meshes(const std::vector<TimedMesh>& input,
                                        const TemporalOptions& options = {});

}  // namespace rosto
