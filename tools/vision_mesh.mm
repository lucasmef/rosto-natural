#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <Vision/Vision.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

struct FPoint { float x{}, y{}; };
using Mesh = std::array<FPoint, 478>;

static FPoint imagePoint(CGPoint p, CGRect box) {
  return {static_cast<float>(box.origin.x + p.x * box.size.width),
          static_cast<float>(1.0 - (box.origin.y + p.y * box.size.height))};
}

static std::vector<FPoint> points(VNFaceLandmarkRegion2D* region, CGRect box) {
  std::vector<FPoint> out;
  if (!region) return out;
  const CGPoint* normalized = region.normalizedPoints;
  for (NSUInteger i = 0; i < region.pointCount; ++i)
    out.push_back(imagePoint(normalized[i], box));
  return out;
}

template <size_t N>
static void assignLoop(Mesh& mesh, const std::array<int, N>& indices,
                       const std::vector<FPoint>& source) {
  if (source.empty()) return;
  for (size_t i = 0; i < N; ++i) {
    const size_t index = (i * source.size()) / N;
    mesh[indices[i]] = source[index % source.size()];
  }
}

static FPoint mean(const std::vector<FPoint>& p) {
  FPoint result{};
  for (auto v : p) { result.x += v.x; result.y += v.y; }
  if (!p.empty()) {
    result.x /= p.size(); result.y /= p.size();
  }
  return result;
}

static std::pair<FPoint, FPoint> horizontalExtremes(const std::vector<FPoint>& p) {
  if (p.empty()) return {};
  const auto left = *std::min_element(p.begin(), p.end(),
      [](auto a, auto b) { return a.x < b.x; });
  const auto right = *std::max_element(p.begin(), p.end(),
      [](auto a, auto b) { return a.x < b.x; });
  return {left, right};
}

static bool writeMesh(VNFaceObservation* face, const char* outputPath) {
  static constexpr std::array<int, 36> oval{
      10,338,297,332,284,251,389,356,454,323,361,288,397,365,379,378,
      400,377,152,148,176,149,150,136,172,58,132,93,234,127,162,21,
      54,103,67,109};
  static constexpr std::array<int, 16> imageLeftEye{
      33,7,163,144,145,153,154,155,133,173,157,158,159,160,161,246};
  static constexpr std::array<int, 16> imageRightEye{
      362,382,381,380,374,373,390,249,263,466,388,387,386,385,384,398};
  static constexpr std::array<int, 20> lips{
      61,146,91,181,84,17,314,405,321,375,291,308,324,318,402,317,
      14,87,178,88};
  static constexpr std::array<int, 5> imageLeftBrow{70,63,105,66,107};
  static constexpr std::array<int, 5> imageRightBrow{336,296,334,293,300};

  const CGRect box = face.boundingBox;
  Mesh mesh{};
  const float cx = box.origin.x + box.size.width * .5;
  const float cy = 1.0 - (box.origin.y + box.size.height * .5);
  for (size_t i = 0; i < oval.size(); ++i) {
    const float theta = static_cast<float>(-M_PI_2 + i * 2.0 * M_PI / oval.size());
    mesh[oval[i]] = {cx + static_cast<float>(box.size.width) * .53F * std::cos(theta),
                     cy + static_cast<float>(box.size.height) * .58F * std::sin(theta)};
  }

  VNFaceLandmarks2D* lm = face.landmarks;
  auto eyeA = points(lm.leftEye, box);
  auto eyeB = points(lm.rightEye, box);
  auto browA = points(lm.leftEyebrow, box);
  auto browB = points(lm.rightEyebrow, box);
  if (mean(eyeA).x > mean(eyeB).x) {
    std::swap(eyeA, eyeB); std::swap(browA, browB);
  }
  assignLoop(mesh, imageLeftEye, eyeA);
  assignLoop(mesh, imageRightEye, eyeB);
  assignLoop(mesh, imageLeftBrow, browA);
  assignLoop(mesh, imageRightBrow, browB);
  const auto [leftA, rightA] = horizontalExtremes(eyeA);
  const auto [leftB, rightB] = horizontalExtremes(eyeB);
  mesh[33] = leftA; mesh[133] = rightA;
  mesh[362] = leftB; mesh[263] = rightB;

  auto outerLips = points(lm.outerLips, box);
  assignLoop(mesh, lips, outerLips);
  auto nose = points(lm.nose, box);
  const auto [noseLeft, noseRight] = horizontalExtremes(nose);
  const FPoint noseMean = mean(nose);
  mesh[1] = noseMean;
  mesh[2] = {noseMean.x, noseMean.y + static_cast<float>(box.size.height) * .035F};
  mesh[98] = noseLeft; mesh[327] = noseRight;
  mesh[205] = {cx - static_cast<float>(box.size.width) * .30F,
               cy + static_cast<float>(box.size.height) * .10F};
  mesh[425] = {cx + static_cast<float>(box.size.width) * .30F,
               cy + static_cast<float>(box.size.height) * .10F};
  mesh[234] = {cx - static_cast<float>(box.size.width) * .50F, cy};
  mesh[454] = {cx + static_cast<float>(box.size.width) * .50F, cy};

  std::ofstream out(outputPath, std::ios::binary);
  const char magic[8]{'R','S','T','M','E','S','H','1'};
  const uint32_t count = 478;
  const float confidence = static_cast<float>(face.confidence);
  out.write(magic, 8);
  out.write(reinterpret_cast<const char*>(&count), sizeof(count));
  out.write(reinterpret_cast<const char*>(&confidence), sizeof(confidence));
  for (const auto& p : mesh) {
    out.write(reinterpret_cast<const char*>(&p.x), sizeof(p.x));
    out.write(reinterpret_cast<const char*>(&p.y), sizeof(p.y));
  }
  return static_cast<bool>(out);
}

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: vision_mesh input_image output_mesh.bin\n";
    return 2;
  }
  @autoreleasepool {
    NSData* data = [NSData dataWithContentsOfFile:
        [NSString stringWithUTF8String:argv[1]]];
    CGImageSourceRef source = CGImageSourceCreateWithData(
        (__bridge CFDataRef)data, nullptr);
    if (!source) return 3;
    CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    VNDetectFaceLandmarksRequest* request = [[VNDetectFaceLandmarksRequest alloc] init];
    request.preferBackgroundProcessing = YES;
    VNImageRequestHandler* handler = [[VNImageRequestHandler alloc]
        initWithCGImage:image options:@{}];
    NSError* error = nil;
    const BOOL ok = [handler performRequests:@[request] error:&error];
    CGImageRelease(image);
    if (!ok || request.results.count == 0) {
      std::cerr << (error ? error.localizedDescription.UTF8String : "no face") << '\n';
      return 4;
    }
    VNFaceObservation* best = request.results.firstObject;
    for (VNFaceObservation* candidate in request.results)
      if (candidate.boundingBox.size.width * candidate.boundingBox.size.height >
          best.boundingBox.size.width * best.boundingBox.size.height) best = candidate;
    return writeMesh(best, argv[2]) ? 0 : 5;
  }
}
