#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "rosto/temporal.hpp"

namespace rosto {

constexpr uint32_t kCacheVersion = 1;
constexpr uint32_t kMeshCacheVersion = 2;

struct CacheMetadata {
  std::string analysis_id;
  std::string source_fingerprint;
  double fps{};
  int width{};
  int height{};
  uint32_t frame_count{};
};

void write_cache(const std::filesystem::path& directory,
                 const CacheMetadata& metadata,
                 const std::vector<TimedFace>& frames);
std::vector<TimedFace> read_landmarks(const std::filesystem::path& binary_path);
void write_mesh_cache(const std::filesystem::path& directory,
                      const CacheMetadata& metadata,
                      const std::vector<TimedMesh>& frames);
std::vector<TimedMesh> read_mesh_landmarks(
    const std::filesystem::path& binary_path);
std::string metadata_json(const CacheMetadata& metadata,
                          uint32_t version = kCacheVersion);

}  // namespace rosto
