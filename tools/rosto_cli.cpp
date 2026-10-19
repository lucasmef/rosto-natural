#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "rosto/retouch.hpp"
#include "rosto/finishing.hpp"

namespace {
rosto::OwnedImage load_ppm(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::string magic;
  int w{}, h{}, max{};
  in >> magic >> w >> h >> max;
  in.get();
  if (!in || magic != "P6" || max != 255) throw std::runtime_error("invalid PPM");
  rosto::OwnedImage image(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      unsigned char rgb[3];
      in.read(reinterpret_cast<char*>(rgb), 3);
      image.view().set(x, y, {rgb[0] / 255.0F, rgb[1] / 255.0F,
                              rgb[2] / 255.0F, 1.0F});
    }
  return image;
}

void save_ppm(const std::string& path, rosto::OwnedImage& image) {
  std::ofstream out(path, std::ios::binary);
  out << "P6\n" << image.width << ' ' << image.height << "\n255\n";
  auto v = image.view();
  for (int y = 0; y < image.height; ++y)
    for (int x = 0; x < image.width; ++x) {
      const auto c = v.get(x, y);
      const unsigned char rgb[3]{
          static_cast<unsigned char>(rosto::clamp01(c.r) * 255.0F),
          static_cast<unsigned char>(rosto::clamp01(c.g) * 255.0F),
          static_cast<unsigned char>(rosto::clamp01(c.b) * 255.0F)};
      out.write(reinterpret_cast<const char*>(rgb), 3);
    }
}

rosto::FaceMesh load_mesh(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  char magic[8]{};
  uint32_t count{};
  rosto::FaceMesh mesh;
  in.read(magic, 8);
  in.read(reinterpret_cast<char*>(&count), sizeof(count));
  in.read(reinterpret_cast<char*>(&mesh.confidence), sizeof(mesh.confidence));
  if (!in || std::string(magic, magic + 8) != "RSTMESH1" ||
      count != rosto::FaceMesh::kLandmarkCount)
    throw std::runtime_error("invalid face mesh");
  for (auto& point : mesh.points) {
    in.read(reinterpret_cast<char*>(&point.x), sizeof(point.x));
    in.read(reinterpret_cast<char*>(&point.y), sizeof(point.y));
  }
  if (!in) throw std::runtime_error("truncated face mesh");
  return mesh;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: rosto_cli input.ppm output.ppm "
                 "[--mesh mesh.bin | center_x center_y width height] "
                 "[--master value] [--skin value] "
                 "[--preserve-texture value] [--under-eye value] "
                 "[--tone value] [--shine value] [--eyes value] "
                 "[--blush value] [--lips value] [--concealer value] [--lip-coverage value]\n";
    return 2;
  }
  try {
    auto image = load_ppm(argv[1]);
    rosto::RetouchParams params;
    rosto::LocalFinishParams local;
    std::string mesh_path;
    std::vector<float> geometry;
    for (int i = 3; i < argc; ++i) {
      const std::string argument = argv[i];
      if (argument == "--mesh" && i + 1 < argc) {
        mesh_path = argv[++i];
      } else if (argument == "--master" && i + 1 < argc) {
        params.master = std::stof(argv[++i]);
      } else if (argument == "--skin" && i + 1 < argc) {
        params.skin = std::stof(argv[++i]);
      } else if (argument == "--preserve-texture" && i + 1 < argc) {
        params.preserve_texture = std::stof(argv[++i]);
      } else if (argument == "--tone" && i + 1 < argc) {
        params.tone = std::stof(argv[++i]);
      } else if (argument == "--under-eye" && i + 1 < argc) {
        params.under_eye = std::stof(argv[++i]);
      } else if (argument == "--shine" && i + 1 < argc) {
        params.shine = std::stof(argv[++i]);
      } else if (argument == "--eyes" && i + 1 < argc) {
        params.eyes = std::stof(argv[++i]);
      } else if (argument == "--blush" && i + 1 < argc) {
        params.blush = std::stof(argv[++i]);
      } else if (argument == "--lips" && i + 1 < argc) {
        params.lips = std::stof(argv[++i]);
      } else if (argument == "--concealer" && i + 1 < argc) {
        local.concealer = std::stof(argv[++i]);
      } else if (argument == "--lip-coverage" && i + 1 < argc) {
        local.lip_coverage = std::stof(argv[++i]);
      } else {
        geometry.push_back(std::stof(argument));
      }
    }
    if (!mesh_path.empty()) {
      if (!geometry.empty()) throw std::runtime_error("unexpected geometry");
      const auto mesh = load_mesh(mesh_path);
      rosto::process(image.view(), mesh, params);
      if (local.concealer > 0 || local.lip_coverage > 0)
        rosto::process_local(image.view(), rosto::local_masks(mesh, image.width, image.height), local);
    } else {
      if (local.concealer > 0 || local.lip_coverage > 0)
        throw std::runtime_error("local finishing requires --mesh");
      rosto::FaceGeometry face{};
      if (!geometry.empty()) {
        if (geometry.size() != 4)
          throw std::runtime_error("expected center_x center_y width height");
        face.center = {geometry[0], geometry[1]};
        face.width = geometry[2]; face.height = geometry[3];
      }
      rosto::process(image.view(), face, params);
    }
    save_ppm(argv[2], image);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
