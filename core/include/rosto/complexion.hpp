#pragma once
#include "rosto/retouch.hpp"

namespace rosto {
// Experimental opt-in calibration of the existing single skin decomposition.
// Defaults preserve the legacy kernel exactly; no geometry or second skin pass.
struct ComplexionParams {
  float detail_reduction{};
  float chin_reduction{};
  float blush_diffusion{};
  float blush_chroma_reduction{};
  float blush_lift{};
};
void process_complexion(ImageView, const FaceMesh&, const RetouchParams&,
                        const ComplexionParams&);
}
