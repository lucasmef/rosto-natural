#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "rosto/cache.hpp"
#include "rosto/flyaway.hpp"
#include "rosto/retouch.hpp"

namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

rosto::OwnedImage test_image(int w = 96, int h = 96) {
  rosto::OwnedImage image(w, h);
  auto v = image.view();
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const float noise = ((x * 17 + y * 29) % 19) / 400.0F;
      v.set(x, y, {0.58F + noise, 0.39F + noise, 0.31F + noise, 1.0F});
    }
  return image;
}

void masks_are_selective() {
  const rosto::FaceGeometry face{};
  require(rosto::sample_masks(0.5F, 0.48F, face).face > 0.9F,
          "face center must be selected");
  require(rosto::sample_masks(0.02F, 0.02F, face).face == 0.0F,
          "background must not be selected");
  require(rosto::sample_masks(0.5F, 0.62F, face).lips > 0.5F,
          "lip region must be selected");
}

void background_is_unchanged() {
  auto image = test_image();
  const auto before = image.pixels;
  rosto::process(image.view(), rosto::FaceGeometry{}, rosto::RetouchParams{});
  for (int y = 0; y < image.height; ++y)
    for (int x = 0; x < image.width; ++x) {
      const auto mask = rosto::sample_masks((x + 0.5F) / image.width,
                                            (y + 0.5F) / image.height, {});
      if (mask.face != 0.0F) continue;
      const size_t i = static_cast<size_t>(y * image.width + x) * 4U;
      for (int c = 0; c < 4; ++c)
        require(image.pixels[i + c] == before[i + c],
                "pixels outside face changed");
    }
}

void bypass_is_exact() {
  auto image = test_image();
  const auto before = image.pixels;
  rosto::RetouchParams p;
  p.before_after = true;
  rosto::process(image.view(), rosto::FaceGeometry{}, p);
  require(image.pixels == before, "before/after bypass must be bit exact");
}

void temporal_gap_is_filled() {
  std::vector<rosto::TimedFace> frames;
  for (int i = 0; i < 7; ++i) {
    rosto::TimedFace f{i / 30.0, rosto::FaceGeometry{}};
    f.face->center.x = 0.45F + i * 0.01F;
    if (i == 3 || i == 4) f.face.reset();
    frames.push_back(f);
  }
  const auto stable = rosto::stabilize_faces(frames);
  require(stable[3].face.has_value() && stable[4].face.has_value(),
          "short temporal gap was not interpolated");
  require(stable[3].face->center.x > 0.45F &&
          stable[3].face->center.x < 0.51F, "interpolation escaped bounds");
}

void cache_round_trip() {
  const auto dir = std::filesystem::temp_directory_path() / "rosto_cache_test";
  std::vector<rosto::TimedFace> frames{{0.0, rosto::FaceGeometry{}},
                                       {1.0 / 30.0, std::nullopt}};
  rosto::CacheMetadata m{"unit-test", "abc", 30.0, 1920, 1080, 2};
  rosto::write_cache(dir, m, frames);
  const auto loaded = rosto::read_landmarks(dir / "landmarks.bin");
  require(loaded.size() == 2 && loaded[0].face && !loaded[1].face,
          "cache round-trip failed");
  std::filesystem::remove_all(dir);
}

void mesh_temporal_and_cache() {
  std::vector<rosto::TimedMesh> frames;
  for (int i = 0; i < 7; ++i) {
    rosto::FaceMesh mesh;
    mesh.confidence = .9F;
    for (auto& point : mesh.points)
      point = {.40F + i * .01F, .50F};
    frames.push_back({i / 30.0, mesh});
  }
  frames[3].mesh.reset();
  frames[4].mesh.reset();
  const auto stable = rosto::stabilize_meshes(frames);
  require(stable[3].mesh && stable[4].mesh,
          "short mesh gap was not interpolated");
  require(stable[3].mesh->points[10].x > .40F &&
          stable[3].mesh->points[10].x < .46F,
          "mesh smoothing escaped bounds");

  const auto dir = std::filesystem::temp_directory_path() /
                   "rosto_mesh_cache_test";
  rosto::CacheMetadata metadata{"mesh-test", "mesh", 30, 1920, 1080,
                                 static_cast<uint32_t>(stable.size())};
  rosto::write_mesh_cache(dir, metadata, stable);
  {
    std::ifstream metadata_file(dir / "metadata.json");
    const std::string json((std::istreambuf_iterator<char>(metadata_file)),
                           std::istreambuf_iterator<char>());
    require(json.find("\"version\": 2") != std::string::npos,
            "mesh metadata contains the wrong cache version");
  }
  const auto loaded = rosto::read_mesh_landmarks(dir / "landmarks.bin");
  require(loaded.size() == stable.size() && loaded[3].mesh,
          "mesh cache round-trip failed");
  require(std::abs(loaded[3].mesh->points[10].x -
                   stable[3].mesh->points[10].x) < 1e-6F,
          "mesh cache changed a landmark");
  std::filesystem::remove_all(dir);
}

void temporal_filter_releases_on_motion() {
  std::vector<rosto::TimedFace> frames;
  for (int i = 0; i < 24; ++i) {
    rosto::FaceGeometry face;
    face.center.x = (i < 12 ? .40F : .55F) +
                    (i % 2 == 0 ? .0015F : -.0015F);
    face.center.y = .50F;
    face.width = .40F;
    face.height = .55F;
    frames.push_back({i / 60.0, face});
  }
  const auto stable = rosto::stabilize_faces(frames);
  require(stable[12].face && stable[12].face->center.x > .52F,
          "adaptive temporal filter lagged behind fast motion");
  const float raw_static_delta = std::abs(
      frames[5].face->center.x - frames[6].face->center.x);
  const float stable_static_delta = std::abs(
      stable[5].face->center.x - stable[6].face->center.x);
  require(stable_static_delta < raw_static_delta * .25F,
          "static temporal jitter was not attenuated");
}

void flyaway_bypass_is_exact() {
  auto image = test_image(48, 32);
  const auto before = image.pixels;
  rosto::OwnedMask candidates(48, 32);
  rosto::OwnedMask protected_hair(48, 32);
  candidates.at(24, 7) = 1.0F;
  rosto::FlyawayParams params;
  params.strength = 0.0F;
  const auto stats = rosto::reconstruct_flyaways(
      image.view(), candidates.view(), protected_hair.view(), params);
  require(stats.reconstructed_pixels == 0 && image.pixels == before,
          "disabled flyaway removal must be bit exact");
}

void flyaway_analysis_separates_mass_and_fringe() {
  rosto::OwnedImage image(64, 64);
  rosto::OwnedMask probability(64, 64);
  for (int y = 24; y < 64; ++y)
    for (int x = 18; x <= 46; ++x)
      probability.at(x, y) = 1.0F;
  // A faint, thin crown wisp that must not become protected hairstyle mass.
  for (int y = 15; y < 24; ++y)
    probability.at(32, y) = .005F;
  probability.at(4, 4) = 1.0F;  // unrelated isolated false positive

  rosto::FlyawayAnalysisParams high;
  high.strength = 1.0F;
  const auto analysis =
      rosto::analyze_flyaways(image.view(), probability.view(), high);
  require(analysis.protected_hair.view().get(30, 32) > .9F,
          "main hairstyle was not protected");
  require(analysis.protected_hair.view().get(32, 18) < .1F,
          "thin crown wisp leaked into protected hairstyle");
  require(analysis.allowed_region.view().get(32, 18) > .9F,
          "high intensity did not select the faint crown wisp");
  require(analysis.allowed_region.view().get(4, 4) < .1F,
          "isolated distant segmentation noise was selected");

  rosto::FlyawayAnalysisParams low = high;
  low.strength = .25F;
  const auto conservative =
      rosto::analyze_flyaways(image.view(), probability.view(), low);
  require(conservative.allowed_region.view().get(32, 18) < .1F,
          "low intensity selected a faint crown wisp");
}

void flyaway_mask_resize_is_feathered() {
  rosto::OwnedMask source(2, 2);
  source.at(0, 0) = 1.0F;
  const auto resized = rosto::resize_mask(source.view(), 4, 4);
  require(resized.view().get(0, 0) > .9F,
          "mask resize lost the selected corner");
  const float feather = resized.view().get(1, 1);
  require(feather > 0.0F && feather < 1.0F,
          "mask resize did not produce a feathered transition");
}

void flyaway_face_guard_is_absolute() {
  rosto::FlyawayAnalysis analysis{
      rosto::OwnedMask(64, 64), rosto::OwnedMask(64, 64)};
  for (float& value : analysis.allowed_region.values) value = 1.0F;
  analysis.allowed_pixels = 64 * 64;
  rosto::FaceMesh mesh;
  mesh.confidence = .95F;
  mesh.points[10] = {.50F, .20F};
  mesh.points[152] = {.50F, .80F};
  mesh.points[234] = {.30F, .50F};
  mesh.points[454] = {.70F, .50F};
  rosto::protect_face_from_flyaways(analysis, mesh);
  require(analysis.allowed_region.view().get(32, 32) == 0.0F &&
              analysis.protected_hair.view().get(32, 32) == 1.0F,
          "flyaway face guard did not make the face immutable");
  require(analysis.allowed_region.view().get(1, 1) == 1.0F &&
              analysis.protected_hair.view().get(1, 1) == 0.0F,
          "flyaway face guard escaped the face oval");
}

void flyaway_reconstruction_is_sparse() {
  rosto::OwnedImage image(48, 32);
  rosto::OwnedMask candidates(48, 32);
  rosto::OwnedMask protected_hair(48, 32);
  auto view = image.view();
  for (int y = 0; y < image.height; ++y)
    for (int x = 0; x < image.width; ++x)
      view.set(x, y, {0.72F + x * .001F, 0.68F, 0.64F, 1.0F});
  for (int y = 10; y < 29; ++y)
    for (int x = 16; x < 33; ++x) {
      view.set(x, y, {.10F, .07F, .05F, 1.0F});
      protected_hair.at(x, y) = 1.0F;
    }
  for (int y = 6; y < 10; ++y) {
    view.set(24, y, {.12F, .08F, .06F, 1.0F});
    candidates.at(24, y) = 1.0F;
  }
  const auto before = image.pixels;
  rosto::FlyawayParams params;
  params.strength = 1.0F;
  params.max_sample_distance_ratio = .30F;
  const auto stats = rosto::reconstruct_flyaways(
      image.view(), candidates.view(), protected_hair.view(), params);
  require(stats.candidate_pixels == 4 && stats.reconstructed_pixels == 4,
          "flyaway candidates were not reconstructed");
  require(image.view().get(24, 7).r > .60F,
          "flyaway pixel did not receive outward background");
  for (int y = 0; y < image.height; ++y)
    for (int x = 0; x < image.width; ++x) {
      if (x == 24 && y >= 6 && y < 10) continue;
      const size_t index = static_cast<size_t>(y * image.width + x) * 4U;
      for (int channel = 0; channel < 4; ++channel)
        require(image.pixels[index + channel] == before[index + channel],
                "flyaway reconstruction changed an unmasked pixel");
    }
}

void flyaway_protected_hair_wins() {
  auto image = test_image(24, 24);
  const auto before = image.pixels;
  rosto::OwnedMask candidates(24, 24);
  rosto::OwnedMask protected_hair(24, 24);
  candidates.at(12, 4) = 1.0F;
  protected_hair.at(12, 4) = 1.0F;
  rosto::FlyawayParams params;
  params.strength = 1.0F;
  const auto stats = rosto::reconstruct_flyaways(
      image.view(), candidates.view(), protected_hair.view(), params);
  require(stats.protected_pixels_skipped == 1 && image.pixels == before,
          "protected hair was modified");
}

void flyaway_temporal_mask_rejects_flicker() {
  rosto::OwnedMask previous(16, 16);
  rosto::OwnedMask missing(16, 16);
  previous.at(8, 4) = 1.0F;
  const auto static_update = rosto::stabilize_flyaway_mask(
      previous.view(), missing.view(), 0.0F);
  require(static_update.values[4 * 16 + 8] > .70F,
          "one-frame mask dropout was not stabilized");
  const auto moving_update = rosto::stabilize_flyaway_mask(
      previous.view(), missing.view(), 1.0F);
  require(moving_update.values[4 * 16 + 8] <
              static_update.values[4 * 16 + 8],
          "temporal mask left a trail during motion");

  rosto::OwnedMask appeared(16, 16);
  appeared.at(6, 3) = 1.0F;
  const auto attack = rosto::stabilize_flyaway_mask(
      missing.view(), appeared.view(), 0.0F);
  require(attack.values[3 * 16 + 6] > .70F,
          "new flyaway mask responded too slowly");
}

void guided_flyaway_preserves_vertical_background() {
  rosto::OwnedImage image(24, 32);
  rosto::OwnedMask allowed(24, 32);
  rosto::OwnedMask protected_hair(24, 32);
  auto view = image.view();
  for (int y = 0; y < image.height; ++y)
    for (int x = 0; x < image.width; ++x) {
      const float curtain = .72F - (x % 7 == 0 ? .08F : 0.0F);
      view.set(x, y, {curtain, curtain, curtain, 1.0F});
      allowed.at(x, y) = 1.0F;
    }
  // Persistent vertical fold: it must remain.
  for (int y = 0; y < image.height; ++y)
    view.set(4, y, {.42F, .42F, .42F, 1.0F});
  // Short dark interruption: it models a curved strand crossing a column.
  for (int y = 10; y <= 12; ++y)
    view.set(10, y, {.18F, .15F, .13F, 1.0F});
  // Same interruption inside protected hair must not change.
  for (int y = 10; y <= 12; ++y) {
    view.set(16, y, {.12F, .08F, .06F, 1.0F});
    protected_hair.at(16, y) = 1.0F;
  }
  const auto before = image.pixels;
  rosto::GuidedFlyawayParams params;
  params.vertical_extent_ratio = .50F;
  const auto stats = rosto::reconstruct_guided_flyaways(
      image.view(), allowed.view(), protected_hair.view(), params);
  require(stats.reconstructed_pixels >= 3,
          "guided reconstruction did not remove the short interruption");
  require(image.view().get(10, 11).r > .60F,
          "guided reconstruction left the simulated strand");
  require(std::abs(image.view().get(4, 11).r - .42F) < 1e-6F,
          "guided reconstruction erased a persistent vertical fold");
  const size_t protected_index = static_cast<size_t>(11 * 24 + 16) * 4U;
  require(image.pixels[protected_index] == before[protected_index],
          "guided reconstruction changed protected hair");
}

void guided_flyaway_intensity_expands_coverage() {
  auto bypass = test_image(24, 32);
  const auto bypass_before = bypass.pixels;
  rosto::OwnedMask bypass_allowed(24, 32);
  rosto::OwnedMask bypass_protected(24, 32);
  bypass_allowed.at(10, 10) = 1.0F;
  rosto::GuidedFlyawayParams bypass_params;
  bypass_params.strength = 0.0F;
  const auto bypass_stats = rosto::reconstruct_guided_flyaways(
      bypass.view(), bypass_allowed.view(), bypass_protected.view(),
      bypass_params);
  require(bypass_stats.reconstructed_pixels == 0 &&
              bypass.pixels == bypass_before,
          "zero guided flyaway intensity must be bit exact");

  rosto::OwnedImage low(24, 32);
  rosto::OwnedImage high(24, 32);
  rosto::OwnedMask allowed(24, 32);
  rosto::OwnedMask protected_hair(24, 32);
  for (int y = 0; y < low.height; ++y)
    for (int x = 0; x < low.width; ++x) {
      const rosto::Color background{.72F, .72F, .72F, 1.0F};
      low.view().set(x, y, background);
      high.view().set(x, y, background);
      allowed.at(x, y) = 1.0F;
    }
  // A faint crown wisp should be ignored conservatively but selected at 100%.
  for (int y = 10; y <= 12; ++y) {
    const rosto::Color faint{.708F, .708F, .708F, 1.0F};
    low.view().set(10, y, faint);
    high.view().set(10, y, faint);
  }
  rosto::GuidedFlyawayParams low_params;
  low_params.strength = .25F;
  low_params.low_vertical_extent_ratio = .50F;
  low_params.vertical_extent_ratio = .50F;
  const auto low_stats = rosto::reconstruct_guided_flyaways(
      low.view(), allowed.view(), protected_hair.view(), low_params);
  rosto::GuidedFlyawayParams high_params = low_params;
  high_params.strength = 1.0F;
  const auto high_stats = rosto::reconstruct_guided_flyaways(
      high.view(), allowed.view(), protected_hair.view(), high_params);
  require(low_stats.reconstructed_pixels == 0,
          "low flyaway intensity selected a faint crown wisp");
  require(high_stats.reconstructed_pixels >= 3,
          "high flyaway intensity did not expand detector coverage");
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2) {
      const auto fixture = rosto::read_mesh_landmarks(argv[1]);
      require(fixture.size() == 2 && fixture[0].mesh && !fixture[1].mesh,
              "UXP mesh cache fixture has an incompatible layout");
      require(std::abs(fixture[0].mesh->points[10].x - .41F) < 1e-6F,
              "UXP mesh cache changed landmark coordinates");
      std::cout << "rosto_core: UXP mesh cache compatible\n";
      return 0;
    }
    masks_are_selective();
    background_is_unchanged();
    bypass_is_exact();
    temporal_gap_is_filled();
    cache_round_trip();
    mesh_temporal_and_cache();
    temporal_filter_releases_on_motion();
    flyaway_bypass_is_exact();
    flyaway_analysis_separates_mass_and_fringe();
    flyaway_mask_resize_is_feathered();
    flyaway_face_guard_is_absolute();
    flyaway_reconstruction_is_sparse();
    flyaway_protected_hair_wins();
    flyaway_temporal_mask_rejects_flicker();
    guided_flyaway_preserves_vertical_background();
    guided_flyaway_intensity_expands_coverage();
    std::cout << "rosto_core: 16 tests passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAILED: " << e.what() << '\n';
    return 1;
  }
}
