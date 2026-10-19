#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// C ABI v1. One context per serial worker; do not share a context concurrently.
// RGB24 contiguous buffers, encoded Rec.709/full range; output written on success
// only. xy contains 478 normalized x,y points. Params: baseline[9], complexion[5],
// local[3]; visibility is width*height floats in [0,1]. No implicit CPU fallback.
void* rn_metal_create(const char* shader, char* error, int capacity);
const char* rn_metal_device(void* context);
int rn_metal_process(void* context, const uint8_t* src, uint8_t* dst, int width,
                     int height, const float* xy, const float* params,
                     const float* visibility, char* error, int capacity);
double rn_metal_gpu_seconds(void* context);
void rn_metal_destroy(void* context);
#ifdef __cplusplus
}
#endif
