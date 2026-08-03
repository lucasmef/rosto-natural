#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define ROSTO_MP_EXPORT __declspec(dllexport)
#else
#define ROSTO_MP_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Stable C boundary between the Adobe effect and the Bazel-built MediaPipe
// runtime. No MediaPipe or Abseil type crosses the dynamic-library boundary.
ROSTO_MP_EXPORT void* rosto_mp_face_create(
    const char* model_path, char* error, size_t error_size);
ROSTO_MP_EXPORT void rosto_mp_face_destroy(void* handle);
ROSTO_MP_EXPORT int rosto_mp_face_detect(
    void* handle, const uint8_t* rgba, int width, int height, int row_bytes,
    float* landmarks_xy, size_t landmark_count, float* confidence,
    char* error, size_t error_size);

ROSTO_MP_EXPORT void* rosto_mp_hair_create(
    const char* model_path, char* error, size_t error_size);
ROSTO_MP_EXPORT void rosto_mp_hair_destroy(void* handle);
ROSTO_MP_EXPORT int rosto_mp_hair_segment(
    void* handle, const uint8_t* rgba, int width, int height, int row_bytes,
    float* hair_probability, size_t probability_count,
    char* error, size_t error_size);

ROSTO_MP_EXPORT const char* rosto_mp_runtime_version(void);

#ifdef __cplusplus
}
#endif
