#include "AEConfig.h"
#ifdef AE_OS_WIN
#include <Windows.h>
#endif
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "String_Utils.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>

#include "rosto/cache.hpp"
#include "rosto/detector.hpp"
#include "rosto/flyaway.hpp"
#include "rosto/retouch.hpp"

#if defined(ROSTO_WITH_MEDIAPIPE) && defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <limits.h>
#endif

namespace {
enum Param {
  INPUT = 0, MASTER, SKIN, TEXTURE, UNDER_EYE, TONE, SHINE, EYES,
  FLYAWAYS, BLUSH, BLUSH_COLOR, LIPS, LIP_COLOR, BEFORE_AFTER,
  ANALYSIS_ID, CAPTURE_MODE, CACHE_VERSION, PARAM_COUNT
};
enum DiskId {
  ID_MASTER = 1, ID_SKIN, ID_TEXTURE, ID_UNDER_EYE, ID_TONE, ID_SHINE,
  ID_EYES, ID_BLUSH, ID_BLUSH_COLOR, ID_LIPS, ID_LIP_COLOR, ID_BEFORE_AFTER,
  ID_ANALYSIS_ID, ID_CAPTURE_MODE, ID_CACHE_VERSION, ID_FLYAWAYS
};

PF_Err about(PF_InData* in, PF_OutData* out) {
  PF_SPRINTF(out->return_msg, "%s v0.11\r%s", "Rosto Natural",
             "Retoque facial e acabamento de cabelo, sem alterar geometria.");
  return PF_Err_NONE;
}

PF_Err global_setup(PF_OutData* out) {
  out->my_version = PF_VERSION(0, 11, 0, PF_Stage_DEVELOP, 1);
  // The current reference renderer samples neighboring pixels and accepts
  // PF_Pixel8 only. Do not advertise pixel independence, smart render, deep
  // color, or float support until those command paths are implemented.
  out->out_flags = PF_OutFlag_NONE;
  out->out_flags2 = PF_OutFlag2_NONE;
  return PF_Err_NONE;
}

PF_Err params_setup(PF_InData* in, PF_OutData* out) {
  PF_Err err = PF_Err_NONE;
  PF_ADD_SLIDER("Intensidade", 0, 100, 0, 100, 72, ID_MASTER);
  PF_ADD_SLIDER("Pele", 0, 100, 0, 100, 58, ID_SKIN);
  PF_ADD_SLIDER("Preservar textura", 0, 100, 0, 100, 75, ID_TEXTURE);
  PF_ADD_SLIDER("Olheiras", 0, 100, 0, 100, 42, ID_UNDER_EYE);
  PF_ADD_SLIDER("Uniformizar tom", 0, 100, 0, 100, 24, ID_TONE);
  PF_ADD_SLIDER("Brilho da pele", 0, 100, 0, 100, 30, ID_SHINE);
  PF_ADD_SLIDER("Olhos", 0, 100, 0, 100, 18, ID_EYES);
  PF_ADD_SLIDER("Cabelos rebeldes", 0, 100, 0, 100, 0, ID_FLYAWAYS);
  PF_ADD_SLIDER("Blush", 0, 100, 0, 100, 0, ID_BLUSH);
  PF_ADD_COLOR("Cor do blush", 219, 87, 102, ID_BLUSH_COLOR);
  PF_ADD_SLIDER("Lábios", 0, 100, 0, 100, 20, ID_LIPS);
  PF_ADD_COLOR("Cor dos lábios", 158, 41, 51, ID_LIP_COLOR);
  PF_ADD_CHECKBOX("Antes / depois", "Mostrar original", FALSE, 0, ID_BEFORE_AFTER);
  PF_ADD_SLIDER("_Analysis ID", 0, 2000000000, 0, 2000000000, 0, ID_ANALYSIS_ID);
  PF_ADD_CHECKBOX("_Capture Mode", "Ativo", FALSE, 0, ID_CAPTURE_MODE);
  PF_ADD_SLIDER("_Cache Version", 1, 100, 1, 100, 3, ID_CACHE_VERSION);
  out->num_params = PARAM_COUNT;
  return err;
}

rosto::RetouchParams read_params(PF_ParamDef* p[]) {
  const auto scalar = [&](Param i) { return p[i]->u.sd.value / 100.0F; };
  rosto::RetouchParams r;
  r.master = scalar(MASTER); r.skin = scalar(SKIN);
  r.preserve_texture = scalar(TEXTURE); r.under_eye = scalar(UNDER_EYE);
  r.tone = scalar(TONE); r.shine = scalar(SHINE); r.eyes = scalar(EYES);
  r.flyaways = scalar(FLYAWAYS);
  r.blush = scalar(BLUSH); r.lips = scalar(LIPS);
  r.blush_color = {p[BLUSH_COLOR]->u.cd.value.red / 255.0F,
                   p[BLUSH_COLOR]->u.cd.value.green / 255.0F,
                   p[BLUSH_COLOR]->u.cd.value.blue / 255.0F, 1.0F};
  r.lip_color = {p[LIP_COLOR]->u.cd.value.red / 255.0F,
                 p[LIP_COLOR]->u.cd.value.green / 255.0F,
                 p[LIP_COLOR]->u.cd.value.blue / 255.0F, 1.0F};
  r.before_after = p[BEFORE_AFTER]->u.bd.value != 0;
  return r;
}

std::filesystem::path analysis_dir(A_long id) {
  return std::filesystem::path("/private/tmp/RostoNatural") / std::to_string(id);
}

void capture_face(A_long id, const rosto::FaceGeometry& face) {
  if (id <= 0) return;
  const auto dir = analysis_dir(id);
  std::filesystem::create_directories(dir);
  const auto pending = dir / "capture.pending";
  {
    std::ofstream out(pending, std::ios::binary);
    out << "{\"version\":1,\"x\":" << face.center.x
        << ",\"y\":" << face.center.y
        << ",\"width\":" << face.width << ",\"height\":" << face.height
        << ",\"roll\":" << face.roll << ",\"confidence\":"
        << face.confidence << "}\n";
  }
  std::filesystem::rename(pending, dir / "capture.json");
}

void capture_mesh(A_long id, const rosto::FaceMesh& mesh) {
  if (id <= 0) return;
  const auto dir = analysis_dir(id);
  std::filesystem::create_directories(dir);
  const auto pending = dir / "capture.pending";
  {
    std::ofstream out(pending, std::ios::binary);
    out << "{\"version\":2,\"confidence\":" << mesh.confidence
        << ",\"points\":[";
    for (size_t index = 0; index < rosto::FaceMesh::kLandmarkCount; ++index) {
      if (index) out << ',';
      out << '[' << mesh.points[index].x << ',' << mesh.points[index].y << ']';
    }
    out << "]}\n";
  }
  std::filesystem::rename(pending, dir / "capture.json");
}

#if defined(ROSTO_WITH_MEDIAPIPE) && defined(__APPLE__)
std::string bundled_model_path(CFStringRef name, CFStringRef extension) {
  CFBundleRef bundle = CFBundleGetBundleWithIdentifier(
      CFSTR("com.lucas.RostoNatural"));
  if (!bundle) return {};
  CFURLRef url = CFBundleCopyResourceURL(bundle, name, extension, nullptr);
  if (!url) return {};
  UInt8 path[PATH_MAX]{};
  const bool ok = CFURLGetFileSystemRepresentation(
      url, true, path, sizeof(path));
  CFRelease(url);
  return ok ? reinterpret_cast<const char*>(path) : std::string{};
}

rosto::OwnedImage analysis_preview(const rosto::ImageView& source) {
  constexpr int kMaximumSide = 640;
  const int longest = std::max(source.width, source.height);
  const float scale =
      longest > kMaximumSide ?
      static_cast<float>(kMaximumSide) / longest : 1.0F;
  const int width = std::max(1, static_cast<int>(source.width * scale));
  const int height = std::max(1, static_cast<int>(source.height * scale));
  rosto::OwnedImage preview(width, height);
  auto view = preview.view();
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      view.set(x, y, source.get(
          std::min(source.width - 1, static_cast<int>(x / scale)),
          std::min(source.height - 1, static_cast<int>(y / scale))));
  return preview;
}

std::optional<rosto::FaceMesh> detect_face(
    const rosto::ImageView& source) {
  static std::mutex mutex;
  static std::unique_ptr<rosto::FaceDetector> detector;
  static bool initialization_attempted = false;
  std::lock_guard<std::mutex> lock(mutex);
  if (!initialization_attempted) {
    initialization_attempted = true;
    const std::string model =
        bundled_model_path(CFSTR("face_landmarker"), CFSTR("task"));
    try {
      if (!model.empty())
        detector = rosto::make_mediapipe_detector(model.c_str());
    } catch (...) {
      detector.reset();
    }
  }
  if (!detector) return std::nullopt;

  rosto::OwnedImage preview = analysis_preview(source);
  try {
    return detector->detect_mesh(preview.view());
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<rosto::FlyawayAnalysis> analyze_hair(
    const rosto::ImageView& source, float strength) {
  if (strength <= 0.0F) return std::nullopt;
  static std::mutex mutex;
  static std::unique_ptr<rosto::HairSegmenter> segmenter;
  static bool initialization_attempted = false;
  std::lock_guard<std::mutex> lock(mutex);
  if (!initialization_attempted) {
    initialization_attempted = true;
    const std::string model =
        bundled_model_path(CFSTR("hair_segmenter"), CFSTR("tflite"));
    try {
      if (!model.empty())
        segmenter = rosto::make_mediapipe_hair_segmenter(model.c_str());
    } catch (...) {
      segmenter.reset();
    }
  }
  if (!segmenter) return std::nullopt;
  try {
    rosto::OwnedImage preview = analysis_preview(source);
    const rosto::OwnedMask probability =
        segmenter->segment_hair(preview.view());
    rosto::FlyawayAnalysisParams params;
    params.strength = strength;
    return rosto::analyze_flyaways(
        preview.view(), probability.view(), params);
  } catch (...) {
    return std::nullopt;
  }
}
#else
std::optional<rosto::FaceMesh> detect_face(const rosto::ImageView&) {
  return std::nullopt;
}
std::optional<rosto::FlyawayAnalysis> analyze_hair(
    const rosto::ImageView&, float) {
  return std::nullopt;
}
#endif

PF_Err render(PF_InData* in, PF_ParamDef* params[], PF_LayerDef* output) {
  PF_Err err = PF_Err_NONE;
  if (params[BEFORE_AFTER]->u.bd.value)
    return PF_COPY(&params[INPUT]->u.ld, output, nullptr, nullptr);
  const int w = output->width, h = output->height;
  rosto::OwnedImage image(w, h);
  const auto* source = reinterpret_cast<const PF_Pixel8*>(params[INPUT]->u.ld.data);
  const int src_stride = params[INPUT]->u.ld.rowbytes / sizeof(PF_Pixel8);
  auto view = image.view();
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const auto& px = source[y * src_stride + x];
      view.set(x, y, {px.red / 255.0F, px.green / 255.0F,
                      px.blue / 255.0F, px.alpha / 255.0F});
    }

  rosto::FaceGeometry face{};
  face.confidence = 0.0F;
  std::optional<rosto::FaceMesh> tracked_mesh;
  const A_long analysis_id = params[ANALYSIS_ID]->u.sd.value;
  const auto cache_file = analysis_dir(analysis_id) / "landmarks.bin";
  if (!params[CAPTURE_MODE]->u.bd.value && std::filesystem::exists(cache_file)) {
    try {
      const auto track = rosto::read_mesh_landmarks(cache_file);
      const double seconds = static_cast<double>(in->current_time) /
          std::max<A_long>(1, in->time_scale);
      auto nearest = std::min_element(track.begin(), track.end(),
          [seconds](const auto& a, const auto& b) {
            return std::abs(a.seconds - seconds) < std::abs(b.seconds - seconds);
          });
      if (nearest != track.end() && nearest->mesh) tracked_mesh = *nearest->mesh;
    } catch (...) {
      try {
        const auto track = rosto::read_landmarks(cache_file);
        const double seconds = static_cast<double>(in->current_time) /
            std::max<A_long>(1, in->time_scale);
        auto nearest = std::min_element(track.begin(), track.end(),
            [seconds](const auto& a, const auto& b) {
              return std::abs(a.seconds - seconds) <
                     std::abs(b.seconds - seconds);
            });
        if (nearest != track.end() && nearest->face) face = *nearest->face;
      } catch (...) { /* Invalid cache: fall back to automatic mode. */ }
    }
  }
  std::optional<rosto::FaceMesh> detected_mesh;
  if (!tracked_mesh) detected_mesh = detect_face(view);
  const rosto::RetouchParams retouch_params = read_params(params);
  const std::optional<rosto::FlyawayAnalysis> hair_analysis =
      !params[CAPTURE_MODE]->u.bd.value ?
      analyze_hair(view, retouch_params.flyaways) : std::nullopt;
  const bool capture_mode = params[CAPTURE_MODE]->u.bd.value != 0;
  if (capture_mode) {
    if (detected_mesh) capture_mesh(analysis_id, *detected_mesh);
    else capture_face(analysis_id, face);
  }
  if (!capture_mode) {
    if (tracked_mesh) rosto::process(view, *tracked_mesh, retouch_params);
    else if (detected_mesh)
      rosto::process(view, *detected_mesh, retouch_params);
    else rosto::process(view, face, retouch_params);
    if (hair_analysis) {
      const rosto::OwnedMask allowed = rosto::resize_mask(
          hair_analysis->allowed_region.view(), w, h);
      const rosto::OwnedMask protected_hair = rosto::resize_mask(
          hair_analysis->protected_hair.view(), w, h);
      rosto::GuidedFlyawayParams hair_params;
      hair_params.strength = retouch_params.flyaways;
      rosto::reconstruct_guided_flyaways(
          view, allowed.view(), protected_hair.view(), hair_params);
    }
  }

  auto* dest = reinterpret_cast<PF_Pixel8*>(output->data);
  const int dst_stride = output->rowbytes / sizeof(PF_Pixel8);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const auto c = view.get(x, y);
      auto& px = dest[y * dst_stride + x];
      px.alpha = static_cast<A_u_char>(rosto::clamp01(c.a) * 255.0F);
      px.red = static_cast<A_u_char>(rosto::clamp01(c.r) * 255.0F);
      px.green = static_cast<A_u_char>(rosto::clamp01(c.g) * 255.0F);
      px.blue = static_cast<A_u_char>(rosto::clamp01(c.b) * 255.0F);
    }
  return err;
}
}  // namespace

extern "C" DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data,
    PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output, void*) {
  try {
    switch (cmd) {
      case PF_Cmd_ABOUT: return about(in_data, out_data);
      case PF_Cmd_GLOBAL_SETUP: return global_setup(out_data);
      case PF_Cmd_PARAMS_SETUP: return params_setup(in_data, out_data);
      case PF_Cmd_RENDER: return render(in_data, params, output);
      default: return PF_Err_NONE;
    }
  } catch (...) {
    return PF_Err_INTERNAL_STRUCT_DAMAGED;
  }
}
