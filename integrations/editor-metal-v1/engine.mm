#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "engine.h"
#include "rosto/retouch_masks.hpp"
#include "rosto/finishing.hpp"
#include <cmath>
#include <cstring>
#include <string>
#include <array>
#include <stdexcept>

namespace {
struct Engine {
 id<MTLDevice> device;id<MTLCommandQueue> queue;
 std::array<id<MTLComputePipelineState>,5> kernels;
 std::array<id<MTLBuffer>,10> buffers;
 std::string name;double gpuSeconds{};
 void allocate(int slot,size_t bytes){bytes=std::max(bytes,size_t(4));if(!buffers[slot]||buffers[slot].length<bytes)buffers[slot]=[device newBufferWithLength:bytes options:MTLResourceStorageModeShared];if(!buffers[slot])throw std::runtime_error("Metal buffer allocation failed");}
};
void error_out(char* dst,int n,const char* msg){if(dst&&n>0)snprintf(dst,n,"%s",msg);}
void check(bool b,const char* msg){if(!b)throw std::invalid_argument(msg);}
float distance(rosto::Point a,rosto::Point b){float x=a.x-b.x,y=a.y-b.y;return std::sqrt(x*x+y*y);}
}
extern "C" void* rn_metal_create(const char* shader,char* error,int capacity){@autoreleasepool {
 try {
  auto e=std::make_unique<Engine>();e->device=MTLCreateSystemDefaultDevice();check(e->device!=nil,"Metal device unavailable (run in a macOS GUI session outside a GPU-restricted sandbox)");
  e->name=e->device.name.UTF8String;e->queue=[e->device newCommandQueue];check(e->queue!=nil,"Metal queue unavailable");
  NSError* err=nil;NSString* src=[NSString stringWithContentsOfFile:[NSString stringWithUTF8String:shader] encoding:NSUTF8StringEncoding error:&err];check(src!=nil,"Cannot read F5 shader");
  MTLCompileOptions* opts=[MTLCompileOptions new];opts.fastMathEnabled=NO;
  id<MTLLibrary> lib=[e->device newLibraryWithSource:src options:opts error:&err];if(!lib)throw std::runtime_error(err.localizedDescription.UTF8String);
  NSArray* names=@[@"unpack",@"blur_h",@"blur_v",@"retouch",@"finish"];
  for(int i=0;i<5;i++){e->kernels[i]=[e->device newComputePipelineStateWithFunction:[lib newFunctionWithName:names[i]] error:&err];if(!e->kernels[i])throw std::runtime_error(err.localizedDescription.UTF8String);}
  return e.release();
 }catch(const std::exception& ex){error_out(error,capacity,ex.what());return nullptr;}
}}
extern "C" const char* rn_metal_device(void* ptr){return ptr?static_cast<Engine*>(ptr)->name.c_str():"";}
extern "C" double rn_metal_gpu_seconds(void* ptr){return ptr?static_cast<Engine*>(ptr)->gpuSeconds:0;}
extern "C" void rn_metal_destroy(void* ptr){delete static_cast<Engine*>(ptr);}
extern "C" int rn_metal_process(void* ptr,const uint8_t* src,uint8_t* dst,int w,int h,const float* xy,const float* params,const float* visibility,char* error,int capacity){@autoreleasepool {
 try {
  check(ptr&&src&&dst&&xy&&params&&visibility,"Null input");check(w>=16&&h>=16&&w<=4096&&h<=4096,"Unsupported dimensions (16..4096)");size_t n=size_t(w)*h;
  for(int i=0;i<17;i++)check(std::isfinite(params[i])&&params[i]>=0&&params[i]<=1,"Parameters must be finite [0,1]");
  check(params[7]==0&&params[8]==0,"This F5 backend requires zero legacy blush/lips");
  for(size_t i=0;i<n;i++)check(std::isfinite(visibility[i])&&visibility[i]>=0&&visibility[i]<=1,"Invalid visibility");
  rosto::FaceMesh mesh;mesh.confidence=1;for(int i=0;i<478;i++){check(std::isfinite(xy[2*i])&&std::isfinite(xy[2*i+1])&&xy[2*i]>=0&&xy[2*i]<=1&&xy[2*i+1]>=0&&xy[2*i+1]<=1,"Invalid landmarks");mesh.points[i]={xy[2*i],xy[2*i+1]};}
  float fw=distance(mesh.points[234],mesh.points[454]),fh=distance(mesh.points[10],mesh.points[152]);check(fw*w>=8&&fh*h>=8,"Degenerate face mesh");
  auto masks=rosto::retouch_mask_grid(mesh,w,h);auto refined=rosto::refined_local_masks(mesh,w,h);auto& e=*static_cast<Engine*>(ptr);
  static_assert(sizeof(rosto::MaskSample)==7*sizeof(float));static_assert(sizeof(rosto::RefinedMask)==6*sizeof(float));
  // Slots: raw, rgba, temporary, fine, coarse, mask grid, refined, visibility,
  // quantized base, final RGB. Reused across frames; no per-frame GPU compilation.
  size_t sizes[]{n*3,n*16,n*16,n*16,n*16,masks.pixels.size()*28,refined.pixels.size()*24,n*4,n*3,n*3};for(int i=0;i<10;i++)e.allocate(i,sizes[i]);
  memcpy(e.buffers[0].contents,src,n*3);memcpy(e.buffers[5].contents,masks.pixels.data(),sizes[5]);if(sizes[6])memcpy(e.buffers[6].contents,refined.pixels.data(),sizes[6]);memcpy(e.buffers[7].contents,visibility,n*4);
  int d[]{w,h,masks.width,masks.height,refined.region.x,refined.region.y,refined.region.width,refined.region.height};
  float p[31]{};std::copy(params,params+17,p);p[17]=fw;p[18]=fh;float roll=std::atan2(mesh.points[454].y-mesh.points[234].y,mesh.points[454].x-mesh.points[234].x);p[19]=std::cos(-roll);p[20]=std::sin(-roll);p[21]=mesh.points[200].x;p[22]=(mesh.points[200].y+mesh.points[152].y)*.5F;
  int ids[]{50,280,205,425};for(int i=0;i<4;i++){p[23+2*i]=mesh.points[ids[i]].x;p[24+2*i]=mesh.points[ids[i]].y;}
  id<MTLCommandBuffer> cb=[e.queue commandBuffer];check(cb!=nil,"Metal command buffer unavailable");
  auto dispatch=[&](int kernel,std::initializer_list<int> slots,int count,int radius=0){
   id<MTLComputeCommandEncoder> enc=[cb computeCommandEncoder];check(enc!=nil,"Metal encoder unavailable");auto state=e.kernels[kernel];[enc setComputePipelineState:state];int index=0;for(int slot:slots)[enc setBuffer:e.buffers[slot] offset:0 atIndex:index++];
   if(kernel==0)[enc setBytes:d length:sizeof(d) atIndex:2];
   else if(kernel==1||kernel==2){[enc setBytes:&radius length:sizeof(radius) atIndex:2];[enc setBytes:d length:sizeof(d) atIndex:3];}
   else {[enc setBytes:p length:sizeof(p) atIndex:4];[enc setBytes:d length:sizeof(d) atIndex:5];[enc setBuffer:e.buffers[kernel==3?8:9] offset:0 atIndex:6];}
   NSUInteger group=std::min(NSUInteger(128),state.maxTotalThreadsPerThreadgroup);[enc dispatchThreads:MTLSizeMake(count,1,1) threadsPerThreadgroup:MTLSizeMake(group,1,1)];[enc endEncoding];
  };
  dispatch(0,{0,1},int(n));int fine=std::max(1,std::min(w,h)/180),coarse=std::max(2,std::min(w,h)/70);
  dispatch(1,{1,2},h,fine);dispatch(2,{2,3},w,fine);dispatch(1,{1,2},h,coarse);dispatch(2,{2,4},w,coarse);
  dispatch(3,{1,3,4,5},int(n));dispatch(4,{8,0,6,7},int(n));[cb commit];[cb waitUntilCompleted];
  if(cb.status!=MTLCommandBufferStatusCompleted)throw std::runtime_error(cb.error?cb.error.localizedDescription.UTF8String:"Metal dispatch failed");
  e.gpuSeconds=cb.GPUEndTime-cb.GPUStartTime;memcpy(dst,e.buffers[9].contents,n*3);return 0;
 }catch(const std::exception& ex){error_out(error,capacity,ex.what());return 1;}
}}
