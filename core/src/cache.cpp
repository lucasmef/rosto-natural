#include "rosto/cache.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace rosto {
namespace {
struct FileHeader {
  char magic[8]{'R','O','S','T','O','L','M','K'};
  uint32_t version{kCacheVersion};
  uint32_t count{};
};
struct FileFrame {
  double seconds{};
  float x{}, y{}, width{}, height{}, roll{}, confidence{};
  uint8_t valid{};
  uint8_t reserved[7]{};
};
std::string escaped(const std::string& s) {
  std::string out;
  for (char ch : s) {
    if (ch == '"' || ch == '\\') out += '\\';
    out += ch;
  }
  return out;
}
}  // namespace

std::string metadata_json(const CacheMetadata& m, uint32_t version) {
  std::ostringstream s;
  s << "{\n"
    << "  \"schema\": \"com.lucas.RostoNatural.analysis\",\n"
    << "  \"version\": " << version << ",\n"
    << "  \"analysisId\": \"" << escaped(m.analysis_id) << "\",\n"
    << "  \"sourceFingerprint\": \"" << escaped(m.source_fingerprint) << "\",\n"
    << "  \"fps\": " << std::setprecision(12) << m.fps << ",\n"
    << "  \"width\": " << m.width << ",\n"
    << "  \"height\": " << m.height << ",\n"
    << "  \"frameCount\": " << m.frame_count << "\n"
    << "}\n";
  return s.str();
}

void write_cache(const std::filesystem::path& directory,
                 const CacheMetadata& metadata,
                 const std::vector<TimedFace>& frames) {
  std::filesystem::create_directories(directory);
  {
    std::ofstream json(directory / "metadata.json", std::ios::binary);
    if (!json) throw std::runtime_error("cannot write metadata.json");
    json << metadata_json(metadata, kCacheVersion);
  }
  std::ofstream out(directory / "landmarks.bin", std::ios::binary);
  if (!out) throw std::runtime_error("cannot write landmarks.bin");
  FileHeader header;
  header.count = static_cast<uint32_t>(frames.size());
  out.write(reinterpret_cast<const char*>(&header), sizeof(header));
  for (const auto& frame : frames) {
    FileFrame f;
    f.seconds = frame.seconds;
    f.valid = frame.face.has_value() ? 1 : 0;
    if (frame.face) {
      f.x = frame.face->center.x; f.y = frame.face->center.y;
      f.width = frame.face->width; f.height = frame.face->height;
      f.roll = frame.face->roll; f.confidence = frame.face->confidence;
    }
    out.write(reinterpret_cast<const char*>(&f), sizeof(f));
  }
}

std::vector<TimedFace> read_landmarks(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot read landmarks.bin");
  FileHeader h;
  in.read(reinterpret_cast<char*>(&h), sizeof(h));
  if (!in || std::string(h.magic, h.magic + 8) != "ROSTOLMK" ||
      h.version != kCacheVersion)
    throw std::runtime_error("unsupported landmark cache");
  std::vector<TimedFace> result;
  result.reserve(h.count);
  for (uint32_t i = 0; i < h.count; ++i) {
    FileFrame f;
    in.read(reinterpret_cast<char*>(&f), sizeof(f));
    if (!in) throw std::runtime_error("truncated landmark cache");
    TimedFace t{f.seconds, std::nullopt};
    if (f.valid) t.face = FaceGeometry{{f.x, f.y}, f.width, f.height,
                                       f.roll, f.confidence};
    result.push_back(t);
  }
  return result;
}

void write_mesh_cache(const std::filesystem::path& directory,
                      const CacheMetadata& metadata,
                      const std::vector<TimedMesh>& frames) {
  std::filesystem::create_directories(directory);
  {
    std::ofstream json(directory / "metadata.json", std::ios::binary);
    if (!json) throw std::runtime_error("cannot write metadata.json");
    json << metadata_json(metadata, kMeshCacheVersion);
  }
  std::ofstream out(directory / "landmarks.bin", std::ios::binary);
  if (!out) throw std::runtime_error("cannot write mesh landmarks.bin");
  const char magic[8]{'R','O','S','T','M','E','S','H'};
  const uint32_t count = static_cast<uint32_t>(frames.size());
  out.write(magic, 8);
  out.write(reinterpret_cast<const char*>(&kMeshCacheVersion), sizeof(uint32_t));
  out.write(reinterpret_cast<const char*>(&count), sizeof(count));
  for (const auto& frame : frames) {
    const uint8_t valid = frame.mesh ? 1 : 0;
    const uint8_t reserved[3]{};
    const float confidence = frame.mesh ? frame.mesh->confidence : 0.0F;
    out.write(reinterpret_cast<const char*>(&frame.seconds), sizeof(double));
    out.write(reinterpret_cast<const char*>(&confidence), sizeof(float));
    out.write(reinterpret_cast<const char*>(&valid), sizeof(valid));
    out.write(reinterpret_cast<const char*>(reserved), sizeof(reserved));
    for (size_t i = 0; i < FaceMesh::kLandmarkCount; ++i) {
      const Point p = frame.mesh ? frame.mesh->points[i] : Point{};
      out.write(reinterpret_cast<const char*>(&p.x), sizeof(float));
      out.write(reinterpret_cast<const char*>(&p.y), sizeof(float));
    }
  }
}

std::vector<TimedMesh> read_mesh_landmarks(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot read mesh landmarks.bin");
  char magic[8]{};
  uint32_t version{}, count{};
  in.read(magic, 8);
  in.read(reinterpret_cast<char*>(&version), sizeof(version));
  in.read(reinterpret_cast<char*>(&count), sizeof(count));
  if (!in || std::string(magic, magic + 8) != "ROSTMESH" ||
      version != kMeshCacheVersion)
    throw std::runtime_error("unsupported mesh landmark cache");
  std::vector<TimedMesh> result;
  result.reserve(count);
  for (uint32_t frame_index = 0; frame_index < count; ++frame_index) {
    TimedMesh frame;
    float confidence{};
    uint8_t valid{}, reserved[3]{};
    FaceMesh mesh;
    in.read(reinterpret_cast<char*>(&frame.seconds), sizeof(double));
    in.read(reinterpret_cast<char*>(&confidence), sizeof(float));
    in.read(reinterpret_cast<char*>(&valid), sizeof(valid));
    in.read(reinterpret_cast<char*>(reserved), sizeof(reserved));
    for (auto& p : mesh.points) {
      in.read(reinterpret_cast<char*>(&p.x), sizeof(float));
      in.read(reinterpret_cast<char*>(&p.y), sizeof(float));
    }
    if (!in) throw std::runtime_error("truncated mesh landmark cache");
    if (valid) {
      mesh.confidence = confidence;
      frame.mesh = mesh;
    }
    result.push_back(frame);
  }
  return result;
}

}  // namespace rosto
