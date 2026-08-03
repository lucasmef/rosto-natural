#include "rosto_mediapipe_bridge.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

#include "mediapipe/framework/formats/image.h"
#include "mediapipe/framework/formats/image_frame.h"
#include "mediapipe/tasks/cc/vision/face_landmarker/face_landmarker.h"
#include "mediapipe/tasks/cc/vision/image_segmenter/image_segmenter.h"

namespace {
using mediapipe::Image;
using mediapipe::ImageFormat;
using mediapipe::ImageFrame;
using mediapipe::tasks::vision::face_landmarker::FaceLandmarker;
using mediapipe::tasks::vision::face_landmarker::FaceLandmarkerOptions;
using mediapipe::tasks::vision::image_segmenter::ImageSegmenter;
using mediapipe::tasks::vision::image_segmenter::ImageSegmenterOptions;

constexpr size_t kFaceLandmarkCount = 478;

void set_error(const std::string& message, char* output, size_t size) {
  if (!output || size == 0) return;
  const size_t count = std::min(size - 1, message.size());
  std::memcpy(output, message.data(), count);
  output[count] = '\0';
}

Image make_image(const uint8_t* rgba, int width, int height, int row_bytes) {
  if (!rgba || width <= 0 || height <= 0 || row_bytes < width * 4)
    throw std::invalid_argument("Invalid RGBA image");
  auto frame =
      std::make_shared<ImageFrame>(ImageFormat::SRGBA, width, height);
  for (int y = 0; y < height; ++y)
    std::memcpy(frame->MutablePixelData() + y * frame->WidthStep(),
                rgba + y * row_bytes, static_cast<size_t>(width) * 4);
  return Image(frame);
}

struct FaceHandle {
  std::unique_ptr<FaceLandmarker> landmarker;
};

struct HairHandle {
  std::unique_ptr<ImageSegmenter> segmenter;
};
}  // namespace

extern "C" {

void* rosto_mp_face_create(
    const char* model_path, char* error, size_t error_size) {
  try {
    if (!model_path || !*model_path)
      throw std::invalid_argument("Face Landmarker model path is empty");
    auto options = std::make_unique<FaceLandmarkerOptions>();
    options->base_options.model_asset_path = model_path;
    options->num_faces = 1;
    options->min_face_detection_confidence = 0.35F;
    options->min_face_presence_confidence = 0.35F;
    options->min_tracking_confidence = 0.35F;
    auto created = FaceLandmarker::Create(std::move(options));
    if (!created.ok()) throw std::runtime_error(created.status().ToString());
    auto handle = std::make_unique<FaceHandle>();
    handle->landmarker = std::move(created.value());
    set_error({}, error, error_size);
    return handle.release();
  } catch (const std::exception& exception) {
    set_error(exception.what(), error, error_size);
    return nullptr;
  } catch (...) {
    set_error("Unknown Face Landmarker initialization error",
              error, error_size);
    return nullptr;
  }
}

void rosto_mp_face_destroy(void* handle) {
  delete static_cast<FaceHandle*>(handle);
}

int rosto_mp_face_detect(
    void* handle, const uint8_t* rgba, int width, int height, int row_bytes,
    float* landmarks_xy, size_t landmark_count, float* confidence,
    char* error, size_t error_size) {
  try {
    if (!handle || !landmarks_xy || landmark_count < kFaceLandmarkCount)
      throw std::invalid_argument("Invalid Face Landmarker output buffer");
    auto result =
        static_cast<FaceHandle*>(handle)->landmarker->Detect(
            make_image(rgba, width, height, row_bytes));
    if (!result.ok()) throw std::runtime_error(result.status().ToString());
    if (result->face_landmarks.empty()) {
      if (confidence) *confidence = 0.0F;
      set_error({}, error, error_size);
      return 0;
    }
    const auto& points = result->face_landmarks.front();
    if (points.size() != kFaceLandmarkCount)
      throw std::runtime_error("Face Landmarker returned an unexpected mesh");
    for (size_t index = 0; index < kFaceLandmarkCount; ++index) {
      landmarks_xy[index * 2] = points[index].x;
      landmarks_xy[index * 2 + 1] = points[index].y;
    }
    if (confidence) *confidence = 1.0F;
    set_error({}, error, error_size);
    return 1;
  } catch (const std::exception& exception) {
    set_error(exception.what(), error, error_size);
    return -1;
  } catch (...) {
    set_error("Unknown Face Landmarker inference error", error, error_size);
    return -1;
  }
}

void* rosto_mp_hair_create(
    const char* model_path, char* error, size_t error_size) {
  try {
    if (!model_path || !*model_path)
      throw std::invalid_argument("Hair Segmenter model path is empty");
    auto options = std::make_unique<ImageSegmenterOptions>();
    options->base_options.model_asset_path = model_path;
    options->output_confidence_masks = true;
    options->output_category_mask = false;
    auto created = ImageSegmenter::Create(std::move(options));
    if (!created.ok()) throw std::runtime_error(created.status().ToString());
    auto handle = std::make_unique<HairHandle>();
    handle->segmenter = std::move(created.value());
    set_error({}, error, error_size);
    return handle.release();
  } catch (const std::exception& exception) {
    set_error(exception.what(), error, error_size);
    return nullptr;
  } catch (...) {
    set_error("Unknown Hair Segmenter initialization error",
              error, error_size);
    return nullptr;
  }
}

void rosto_mp_hair_destroy(void* handle) {
  delete static_cast<HairHandle*>(handle);
}

int rosto_mp_hair_segment(
    void* handle, const uint8_t* rgba, int width, int height, int row_bytes,
    float* hair_probability, size_t probability_count,
    char* error, size_t error_size) {
  try {
    const size_t expected =
        static_cast<size_t>(std::max(0, width)) *
        static_cast<size_t>(std::max(0, height));
    if (!handle || !hair_probability || probability_count < expected)
      throw std::invalid_argument("Invalid Hair Segmenter output buffer");
    auto result =
        static_cast<HairHandle*>(handle)->segmenter->Segment(
            make_image(rgba, width, height, row_bytes));
    if (!result.ok()) throw std::runtime_error(result.status().ToString());
    if (!result->confidence_masks || result->confidence_masks->size() < 2)
      throw std::runtime_error("Hair Segmenter returned no hair mask");
    const auto frame =
        result->confidence_masks->at(1).GetImageFrameSharedPtr();
    if (!frame || frame->Width() != width || frame->Height() != height)
      throw std::runtime_error("Hair Segmenter returned an invalid mask");
    for (int y = 0; y < height; ++y) {
      const auto* row = reinterpret_cast<const float*>(
          frame->PixelData() + y * frame->WidthStep());
      for (int x = 0; x < width; ++x)
        hair_probability[static_cast<size_t>(y) * width + x] =
            std::clamp(row[x], 0.0F, 1.0F);
    }
    set_error({}, error, error_size);
    return 1;
  } catch (const std::exception& exception) {
    set_error(exception.what(), error, error_size);
    return -1;
  } catch (...) {
    set_error("Unknown Hair Segmenter inference error", error, error_size);
    return -1;
  }
}

const char* rosto_mp_runtime_version(void) {
  return "mediapipe-0.10.35/rosto-bridge-1";
}

}  // extern "C"
