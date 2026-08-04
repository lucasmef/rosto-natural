#include "AEConfig.h"
#ifdef AE_OS_WIN
#include <Windows.h>
#endif
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "RostoNaturalVersion.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <locale>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <system_error>

#if !defined(AE_OS_WIN)
#include <unistd.h>
#endif

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
  INPUT = 0,
  MASTER = 1,
  SKIN = 2,
  TEXTURE = 3,
  UNDER_EYE = 4,
  TONE = 5,
  SHINE = 6,
  EYES = 7,
  FLYAWAYS = 8,
  BLUSH = 9,
  BLUSH_COLOR = 10,
  LIPS = 11,
  LIP_COLOR = 12,
  BEFORE_AFTER = 13,
  ANALYSIS_ID = 14,
  CAPTURE_MODE = 15,
  CACHE_VERSION = 16,
  CAPTURE_TOKEN = 17,
  CAPTURE_SESSION = 18,
  PARAM_COUNT = 19
};
enum DiskId {
  ID_MASTER = 1,
  ID_SKIN = 2,
  ID_TEXTURE = 3,
  ID_UNDER_EYE = 4,
  ID_TONE = 5,
  ID_SHINE = 6,
  ID_EYES = 7,
  ID_BLUSH = 8,
  ID_BLUSH_COLOR = 9,
  ID_LIPS = 10,
  ID_LIP_COLOR = 11,
  ID_BEFORE_AFTER = 12,
  ID_ANALYSIS_ID = 13,
  ID_CAPTURE_MODE = 14,
  ID_CACHE_VERSION = 15,
  ID_FLYAWAYS = 16,
  ID_CAPTURE_TOKEN = 17,
  ID_CAPTURE_SESSION = 18
};
static_assert(ID_FLYAWAYS == 16 && ID_CAPTURE_TOKEN == 17 &&
              ID_CAPTURE_SESSION == 18,
              "Never renumber persisted Adobe parameter disk IDs");
static_assert(rosto::kMeshCacheVersion == 2,
              "Keep the native default aligned with the mesh cache schema");

PF_Err about(PF_InData* in_data, PF_OutData* out_data) {
  PF_SPRINTF(out_data->return_msg, "%s v0.11\r%s", "Rosto Natural",
             "Retoque facial e acabamento de cabelo, sem alterar geometria.");
  return PF_Err_NONE;
}

PF_Err global_setup(PF_OutData* out) {
  out->my_version = ROSTO_EFFECT_VERSION;
  // The current reference renderer samples neighboring pixels and accepts
  // PF_Pixel8 only. Do not advertise pixel independence, smart render, deep
  // color, or float support until those command paths are implemented.
  // The external landmark cache is sampled by current_time. Even on a still
  // source, different host times can therefore produce different pixels.
  out->out_flags = PF_OutFlag_NON_PARAM_VARY;
  out->out_flags2 = PF_OutFlag2_NONE;
  return PF_Err_NONE;
}

PF_Err params_setup(PF_InData* in_data, PF_OutData* out_data) {
  PF_Err err = PF_Err_NONE;
  PF_ParamDef def;
  AEFX_CLR_STRUCT(def);
  PF_ADD_SLIDER("Intensidade", 0, 100, 0, 100, 72, ID_MASTER);
  AEFX_CLR_STRUCT(def);
  PF_ADD_SLIDER("Pele", 0, 100, 0, 100, 58, ID_SKIN);
  AEFX_CLR_STRUCT(def);
  PF_ADD_SLIDER("Preservar textura", 0, 100, 0, 100, 75, ID_TEXTURE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_SLIDER("Olheiras", 0, 100, 0, 100, 42, ID_UNDER_EYE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_SLIDER("Uniformizar tom", 0, 100, 0, 100, 24, ID_TONE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_SLIDER("Brilho da pele", 0, 100, 0, 100, 30, ID_SHINE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_SLIDER("Olhos", 0, 100, 0, 100, 18, ID_EYES);
  AEFX_CLR_STRUCT(def);
  PF_ADD_SLIDER("Cabelos rebeldes", 0, 100, 0, 100, 0, ID_FLYAWAYS);
  AEFX_CLR_STRUCT(def);
  PF_ADD_SLIDER("Blush", 0, 100, 0, 100, 0, ID_BLUSH);
  AEFX_CLR_STRUCT(def);
  PF_ADD_COLOR("Cor do blush", 219, 87, 102, ID_BLUSH_COLOR);
  AEFX_CLR_STRUCT(def);
  PF_ADD_SLIDER("Lábios", 0, 100, 0, 100, 20, ID_LIPS);
  AEFX_CLR_STRUCT(def);
  PF_ADD_COLOR("Cor dos lábios", 158, 41, 51, ID_LIP_COLOR);
  AEFX_CLR_STRUCT(def);
  PF_ADD_CHECKBOX("Antes / depois", "Mostrar original", FALSE, 0, ID_BEFORE_AFTER);
  AEFX_CLR_STRUCT(def);
  PF_ADD_SLIDER("_Analysis ID", 0, 2000000000, 0, 2000000000, 0, ID_ANALYSIS_ID);
  AEFX_CLR_STRUCT(def);
  PF_ADD_CHECKBOX("_Capture Mode", "Ativo", FALSE, 0, ID_CAPTURE_MODE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_SLIDER("_Cache Version", 1, 100, 1, 100, 2, ID_CACHE_VERSION);
  AEFX_CLR_STRUCT(def);
  PF_ADD_SLIDER("_Capture Token", 0, 2000000000, 0, 2000000000, 0,
                ID_CAPTURE_TOKEN);
  AEFX_CLR_STRUCT(def);
  PF_ADD_SLIDER("_Capture Session", 0, 2000000000, 0, 2000000000, 0,
                ID_CAPTURE_SESSION);
  out_data->num_params = PARAM_COUNT;
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

bool capture_lease_is_active(A_long session_id) noexcept {
  if (session_id <= 0) return false;
  try {
    const auto lease = analysis_dir(session_id) / "capture.lease";
    std::error_code error;
    const auto modified = std::filesystem::last_write_time(lease, error);
    if (error) return false;
    const auto age = std::filesystem::file_time_type::clock::now() - modified;
    return age >= decltype(age)::zero() &&
        age <= std::chrono::seconds(10);
  } catch (...) {
    return false;
  }
}

std::mutex capture_file_mutex;
std::atomic<unsigned long long> capture_file_serial{0};

unsigned long capture_process_id() {
#if defined(AE_OS_WIN)
  return static_cast<unsigned long>(GetCurrentProcessId());
#else
  return static_cast<unsigned long>(getpid());
#endif
}

template <typename PayloadWriter>
void publish_capture(A_long id, A_long token, A_long host_time,
                     A_u_long host_time_scale, int width, int height,
                     int quality, int field, int version,
                     PayloadWriter&& write_payload) noexcept {
  if (id <= 0 || token <= 0 || host_time_scale == 0 ||
      width <= 0 || height <= 0 || !capture_lease_is_active(id)) return;
  try {
    std::lock_guard<std::mutex> lock(capture_file_mutex);
    // The detector and the render scheduler may outlive the panel request.
    // Re-check under the publication lock so a removed/expired lease cannot
    // create a late capture after cleanup or a panel crash.
    if (!capture_lease_is_active(id)) return;
    const auto dir = analysis_dir(id);
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    if (error) return;

    const auto serial = capture_file_serial.fetch_add(
        1, std::memory_order_relaxed);
    const std::string base =
        "capture-" + std::to_string(token) + "-" +
        std::to_string(host_time) + "-" +
        std::to_string(host_time_scale) + "-" +
        std::to_string(width) + "x" + std::to_string(height) + "-q" +
        std::to_string(quality) + "-f" + std::to_string(field) + "-p" +
        std::to_string(capture_process_id()) + "-s" +
        std::to_string(serial);
    const auto pending = dir / (base + ".pending");
    const auto published = dir / (base + ".json");

    std::ofstream out(pending, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return;
    out.imbue(std::locale::classic());
    out << "{\"version\":" << version
        << ",\"captureToken\":" << token
        << ",\"hostTime\":" << host_time
        << ",\"hostTimeScale\":" << host_time_scale
        << ",\"renderWidth\":" << width
        << ",\"renderHeight\":" << height
        << ",\"quality\":" << quality
        << ",\"highQuality\":"
        << (quality == PF_Quality_HI ? "true" : "false")
        << ",\"field\":" << field;
    write_payload(out);
    out << "}\n";
    out.flush();
    const bool write_ok = out.good();
    out.close();
    if (!write_ok || out.fail()) {
      std::filesystem::remove(pending, error);
      return;
    }
    if (!capture_lease_is_active(id)) {
      std::filesystem::remove(pending, error);
      return;
    }
    std::filesystem::rename(pending, published, error);
    if (error) std::filesystem::remove(pending, error);
  } catch (...) {
    // Capture is a diagnostic side channel. It must never break rendering.
  }
}

void capture_face(A_long id, A_long token, const rosto::FaceGeometry& face,
                  A_long host_time, A_u_long host_time_scale,
                  int width, int height, int quality, int field) {
  if (!std::isfinite(face.center.x) || !std::isfinite(face.center.y) ||
      !std::isfinite(face.width) || !std::isfinite(face.height) ||
      !std::isfinite(face.roll) || !std::isfinite(face.confidence)) return;
  publish_capture(id, token, host_time, host_time_scale, width, height,
                  quality, field, 1, [&](std::ostream& out) {
    out << ",\"x\":" << face.center.x
        << ",\"y\":" << face.center.y
        << ",\"width\":" << face.width
        << ",\"height\":" << face.height
        << ",\"roll\":" << face.roll
        << ",\"confidence\":" << face.confidence;
  });
}

void capture_mesh(A_long id, A_long token, const rosto::FaceMesh& mesh,
                  A_long host_time, A_u_long host_time_scale,
                  int width, int height, int quality, int field) {
  if (!std::isfinite(mesh.confidence) ||
      !std::all_of(mesh.points.begin(), mesh.points.end(), [](const auto& p) {
        return std::isfinite(p.x) && std::isfinite(p.y);
      })) return;
  publish_capture(id, token, host_time, host_time_scale, width, height,
                  quality, field, 2, [&](std::ostream& out) {
    out << ",\"confidence\":" << mesh.confidence << ",\"points\":[";
    for (size_t index = 0; index < rosto::FaceMesh::kLandmarkCount; ++index) {
      if (index) out << ',';
      out << '[' << mesh.points[index].x << ',' << mesh.points[index].y << ']';
    }
    out << ']';
  });
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
    const rosto::ImageView& source, float strength,
    const rosto::FaceMesh* face_mesh) {
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
    rosto::FlyawayAnalysis analysis = rosto::analyze_flyaways(
        preview.view(), probability.view(), params);
    if (face_mesh)
      rosto::protect_face_from_flyaways(analysis, *face_mesh);
    return analysis;
  } catch (...) {
    return std::nullopt;
  }
}
#else
std::optional<rosto::FaceMesh> detect_face(const rosto::ImageView&) {
  return std::nullopt;
}
std::optional<rosto::FlyawayAnalysis> analyze_hair(
    const rosto::ImageView&, float, const rosto::FaceMesh*) {
  return std::nullopt;
}
#endif

PF_Err render(PF_InData* in_data, PF_ParamDef* params[],
              PF_LayerDef* output) {
  PF_Err err = PF_Err_NONE;
  const A_long capture_session = params[CAPTURE_SESSION]->u.sd.value;
  const bool capture_mode = params[CAPTURE_MODE]->u.bd.value != 0 &&
      capture_lease_is_active(capture_session);
  if (params[BEFORE_AFTER]->u.bd.value && !capture_mode)
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
  const A_long cache_version = params[CACHE_VERSION]->u.sd.value;
  const auto cache_file = analysis_dir(analysis_id) / "landmarks.bin";
  std::error_code cache_error;
  const bool cache_exists = analysis_id > 0 &&
      (cache_version == 1 || cache_version == rosto::kMeshCacheVersion) &&
      std::filesystem::exists(cache_file, cache_error);
  if (!cache_error && cache_exists) {
    try {
      const double seconds = static_cast<double>(in_data->current_time) /
          std::max<A_u_long>(1, in_data->time_scale);
      if (cache_version == rosto::kMeshCacheVersion) {
        const auto track = rosto::read_mesh_landmarks(cache_file);
        auto nearest = std::min_element(track.begin(), track.end(),
            [seconds](const auto& a, const auto& b) {
              return std::abs(a.seconds - seconds) <
                     std::abs(b.seconds - seconds);
            });
        if (nearest != track.end() && nearest->mesh)
          tracked_mesh = *nearest->mesh;
      } else {
        const auto track = rosto::read_landmarks(cache_file);
        auto nearest = std::min_element(track.begin(), track.end(),
            [seconds](const auto& a, const auto& b) {
              return std::abs(a.seconds - seconds) <
                     std::abs(b.seconds - seconds);
            });
        if (nearest != track.end() && nearest->face) face = *nearest->face;
      }
    } catch (...) { /* Invalid cache: fall back to automatic mode. */ }
  }
  const bool has_tracked_geometry = face.confidence > 0.0F;
  const bool needs_render_detection =
      !tracked_mesh && !has_tracked_geometry;
  std::optional<rosto::FaceMesh> detected_mesh;
  if (capture_mode || needs_render_detection)
    detected_mesh = detect_face(view);
  const rosto::RetouchParams retouch_params = read_params(params);
  if (capture_mode) {
    const A_long capture_token = params[CAPTURE_TOKEN]->u.sd.value;
    if (detected_mesh)
      capture_mesh(capture_session, capture_token, *detected_mesh,
                   in_data->current_time, in_data->time_scale, w, h,
                   static_cast<int>(in_data->quality),
                   0);
    else {
      rosto::FaceGeometry no_face{};
      no_face.confidence = 0.0F;
      capture_face(capture_session, capture_token, no_face,
                   in_data->current_time, in_data->time_scale, w, h,
                   static_cast<int>(in_data->quality),
                   0);
    }
  }
  const rosto::FaceMesh* face_for_hair = tracked_mesh ? &*tracked_mesh :
      (needs_render_detection && detected_mesh ? &*detected_mesh : nullptr);
  const std::optional<rosto::FlyawayAnalysis> hair_analysis =
      !retouch_params.before_after ?
      analyze_hair(view, retouch_params.flyaways, face_for_hair) :
      std::nullopt;
  // Capture is a side channel. Its lease never changes the rendered pixels:
  // tracked caches remain authoritative, and before/after is an exact copy.
  if (retouch_params.before_after)
    return PF_COPY(&params[INPUT]->u.ld, output, nullptr, nullptr);
  if (!retouch_params.before_after) {
    if (tracked_mesh) rosto::process(view, *tracked_mesh, retouch_params);
    else if (needs_render_detection && detected_mesh)
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

extern "C" DllExport PF_Err PluginDataEntryFunction2(
    PF_PluginDataPtr plugin_data,
    PF_PluginDataCB2 plugin_data_callback,
    SPBasicSuite*, const char*, const char*) {
  if (!plugin_data || !plugin_data_callback)
    return PF_Err_INVALID_CALLBACK;
  PF_Err result = PF_Err_INVALID_CALLBACK;
  result = PF_REGISTER_EFFECT_EXT2(
      plugin_data,
      plugin_data_callback,
      "Rosto Natural",
      "com.lucas.RostoNatural",
      "Lucas",
      AE_RESERVED_INFO,
      "EffectMain",
      "https://github.com/lucasmef/rosto-natural");
  return result;
}

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
  } catch (const std::bad_alloc&) {
    return PF_Err_OUT_OF_MEMORY;
  } catch (...) {
    return PF_Err_INTERNAL_STRUCT_DAMAGED;
  }
}
