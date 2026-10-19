#pragma once
#include "rosto/retouch.hpp"

namespace rosto {
// Read-only export of the exact CPU mask grid, for equivalent GPU execution.
// No change to the legacy process path or its interpolation conventions.
struct RetouchMaskGrid {
  int width{}, height{};
  std::vector<MaskSample> pixels;
};
RetouchMaskGrid retouch_mask_grid(const FaceMesh&, int width, int height);
}
