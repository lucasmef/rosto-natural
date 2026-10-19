#include "rosto/retouch.hpp"
#include "rosto/complexion.hpp"
#include "rosto/retouch_masks.hpp"

#include <cmath>
#include <functional>
#include <vector>

namespace rosto {
namespace {

float smoothstep(float a, float b, float x) {
  const float t = clamp01((x - a) / (b - a));
  return t * t * (3.0F - 2.0F * t);
}

float ellipse(float x, float y, float cx, float cy, float rx, float ry,
              float feather = 0.20F) {
  const float dx = (x - cx) / rx;
  const float dy = (y - cy) / ry;
  const float d = std::sqrt(dx * dx + dy * dy);
  return 1.0F - smoothstep(1.0F - feather, 1.0F, d);
}

Color mix(Color a, Color b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
          a.b + (b.b - a.b) * t, a.a};
}

float luma(Color c) { return 0.2126F * c.r + 0.7152F * c.g + 0.0722F * c.b; }

float point_distance(Point a, Point b) {
  const float x = a.x - b.x, y = a.y - b.y;
  return std::sqrt(x * x + y * y);
}

Point average_points(const FaceMesh& mesh, const int* indices, size_t count) {
  Point p{};
  for (size_t i = 0; i < count; ++i) {
    p.x += mesh.points[indices[i]].x;
    p.y += mesh.points[indices[i]].y;
  }
  p.x /= static_cast<float>(count); p.y /= static_cast<float>(count);
  return p;
}

float oriented_ellipse(float x, float y, Point center, float rx, float ry,
                       float angle, float feather = .28F) {
  const float c = std::cos(-angle), s = std::sin(-angle);
  const float dx = x - center.x, dy = y - center.y;
  const float qx = (c * dx - s * dy) / std::max(rx, 1e-5F);
  const float qy = (s * dx + c * dy) / std::max(ry, 1e-5F);
  return 1.0F - smoothstep(1.0F - feather, 1.0F,
                           std::sqrt(qx * qx + qy * qy));
}

template <size_t N>
float signed_polygon_distance(float x, float y, const FaceMesh& mesh,
                              const std::array<int, N>& indices) {
  bool inside = false;
  float min_distance = 10.0F;
  const Point p{x, y};
  for (size_t i = 0, j = N - 1; i < N; j = i++) {
    const Point a = mesh.points[indices[j]], b = mesh.points[indices[i]];
    const bool crossing = ((a.y > y) != (b.y > y)) &&
        (x < (b.x - a.x) * (y - a.y) / (b.y - a.y + 1e-12F) + a.x);
    if (crossing) inside = !inside;
    const float vx = b.x - a.x, vy = b.y - a.y;
    const float length2 = vx * vx + vy * vy;
    const float t = length2 > 0 ? std::clamp(
        ((p.x - a.x) * vx + (p.y - a.y) * vy) / length2, 0.0F, 1.0F) : 0.0F;
    min_distance = std::min(min_distance,
        point_distance(p, {a.x + vx * t, a.y + vy * t}));
  }
  return inside ? min_distance : -min_distance;
}

template <size_t N>
float soft_polygon(float x, float y, const FaceMesh& mesh,
                   const std::array<int, N>& indices, float feather) {
  return smoothstep(-feather, feather,
                    signed_polygon_distance(x, y, mesh, indices));
}

template <size_t N>
float inner_polygon(float x, float y, const FaceMesh& mesh,
                    const std::array<int, N>& indices, float feather) {
  // Treatment must never extend beyond the face contour. Feather exclusively
  // inward so blurred skin cannot contaminate hair, jaw or background.
  return smoothstep(0.0F, feather,
                    signed_polygon_distance(x, y, mesh, indices));
}

struct MeshMaskMap {
  int width{}, height{};
  std::vector<MaskSample> pixels;

  MaskSample sample(float nx, float ny) const {
    const float fx = clamp01(nx) * (width - 1);
    const float fy = clamp01(ny) * (height - 1);
    const int x0 = static_cast<int>(fx), y0 = static_cast<int>(fy);
    const int x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
    const float tx = fx - x0, ty = fy - y0;
    const MaskSample& a = pixels[y0 * width + x0];
    const MaskSample& b = pixels[y0 * width + x1];
    const MaskSample& c = pixels[y1 * width + x0];
    const MaskSample& d = pixels[y1 * width + x1];
    const auto blend = [&](float MaskSample::*member) {
      const float top = a.*member + (b.*member - a.*member) * tx;
      const float bottom = c.*member + (d.*member - c.*member) * tx;
      return top + (bottom - top) * ty;
    };
    return {blend(&MaskSample::face), blend(&MaskSample::skin),
            blend(&MaskSample::under_eye), blend(&MaskSample::eyes),
            blend(&MaskSample::brows), blend(&MaskSample::blush),
            blend(&MaskSample::lips)};
  }
};

MeshMaskMap build_mesh_masks(const FaceMesh& mesh, int image_width,
                             int image_height) {
  static constexpr std::array<int, 36> oval{
      10,338,297,332,284,251,389,356,454,323,361,288,397,365,379,378,
      400,377,152,148,176,149,150,136,172,58,132,93,234,127,162,21,
      54,103,67,109};
  static constexpr std::array<int, 16> left_eye{
      33,7,163,144,145,153,154,155,133,173,157,158,159,160,161,246};
  static constexpr std::array<int, 16> right_eye{
      362,382,381,380,374,373,390,249,263,466,388,387,386,385,384,398};
  static constexpr std::array<int, 20> lips{
      61,146,91,181,84,17,314,405,321,375,291,308,324,318,402,317,
      14,87,178,88};
  static constexpr std::array<int, 5> left_brow_mesh{70,63,105,66,107};
  static constexpr std::array<int, 5> right_brow_mesh{336,296,334,293,300};
  static constexpr int left_eye_center_ids[]{33, 133, 159, 145};
  static constexpr int right_eye_center_ids[]{362, 263, 386, 374};
  static constexpr int left_brow_ids[]{70, 63, 105, 66, 107};
  static constexpr int right_brow_ids[]{336, 296, 334, 293, 300};

  MeshMaskMap map;
  const float aspect = static_cast<float>(image_width) / image_height;
  if (image_width >= image_height) {
    map.width = 512; map.height = std::max(64, static_cast<int>(512 / aspect));
  } else {
    map.height = 512; map.width = std::max(64, static_cast<int>(512 * aspect));
  }
  map.pixels.resize(static_cast<size_t>(map.width) * map.height);

  const Point left_eye_center = average_points(mesh, left_eye_center_ids, 4);
  const Point right_eye_center = average_points(mesh, right_eye_center_ids, 4);
  const Point left_brow = average_points(mesh, left_brow_ids, 5);
  const Point right_brow = average_points(mesh, right_brow_ids, 5);
  const float left_eye_width = point_distance(mesh.points[33], mesh.points[133]);
  const float right_eye_width = point_distance(mesh.points[362], mesh.points[263]);
  const float left_angle = std::atan2(mesh.points[133].y - mesh.points[33].y,
                                      mesh.points[133].x - mesh.points[33].x);
  const float right_angle = std::atan2(mesh.points[263].y - mesh.points[362].y,
                                       mesh.points[263].x - mesh.points[362].x);
  const Point left_protect{(left_eye_center.x + left_brow.x) * .5F,
                           (left_eye_center.y + left_brow.y) * .5F};
  const Point right_protect{(right_eye_center.x + right_brow.x) * .5F,
                            (right_eye_center.y + right_brow.y) * .5F};
  const Point left_under{left_eye_center.x,
                         left_eye_center.y + left_eye_width * .21F};
  const Point right_under{right_eye_center.x,
                          right_eye_center.y + right_eye_width * .21F};
  const Point nose_center{(mesh.points[1].x + mesh.points[2].x) * .5F,
                          (mesh.points[1].y + mesh.points[2].y) * .5F};
  const float nose_width = point_distance(mesh.points[98], mesh.points[327]);
  const float face_width = point_distance(mesh.points[234], mesh.points[454]);
  const float face_height = point_distance(mesh.points[10], mesh.points[152]);
  const Point left_cheek = mesh.points[205], right_cheek = mesh.points[425];
  const float feature_feather = std::max(.002F, face_width * .010F);

  for (int py = 0; py < map.height; ++py) {
    const float y = (py + .5F) / map.height;
    for (int px = 0; px < map.width; ++px) {
      const float x = (px + .5F) / map.width;
      const float oval_distance =
          signed_polygon_distance(x, y, mesh, oval);
      const float face = smoothstep(
          0.0F, face_width * .012F, oval_distance);
      if (face < .0001F) continue;
      // Smoothing fades in farther inside the face than color treatment. This
      // protects the jaw edge without leaving a visible inner mask seam.
      const float smoothing_face = smoothstep(
          0.0F, face_width * .045F, oval_distance);
      const float eye_l = soft_polygon(x, y, mesh, left_eye, feature_feather);
      const float eye_r = soft_polygon(x, y, mesh, right_eye, feature_feather);
      const float eye = clamp01(eye_l + eye_r);
      const float brow = clamp01(
          soft_polygon(x, y, mesh, left_brow_mesh, feature_feather) +
          soft_polygon(x, y, mesh, right_brow_mesh, feature_feather));
      const float protect_l = oriented_ellipse(
          x, y, left_protect, left_eye_width * .72F,
          std::max(left_eye_width * .30F,
                   point_distance(left_eye_center, left_brow) * .82F),
          left_angle);
      const float protect_r = oriented_ellipse(
          x, y, right_protect, right_eye_width * .72F,
          std::max(right_eye_width * .30F,
                   point_distance(right_eye_center, right_brow) * .82F),
          right_angle);
      const float eye_protect = clamp01(protect_l + protect_r);
      const float lip = soft_polygon(x, y, mesh, lips, feature_feather);
      const float nose = oriented_ellipse(x, y, nose_center,
          nose_width * .58F, face_height * .21F, 0.0F, .35F);
      const float under = clamp01(
          oriented_ellipse(x, y, left_under, left_eye_width * .62F,
                           left_eye_width * .24F, left_angle, .40F) +
          oriented_ellipse(x, y, right_under, right_eye_width * .62F,
                           right_eye_width * .24F, right_angle, .40F));
      const float blush = clamp01(
          oriented_ellipse(x, y, left_cheek, face_width * .13F,
                           face_height * .075F, left_angle, .48F) +
          oriented_ellipse(x, y, right_cheek, face_width * .13F,
                           face_height * .075F, right_angle, .48F));
      map.pixels[py * map.width + px] = {
          face, smoothing_face * (1.0F - eye_protect) * (1.0F - eye) *
                    (1.0F - brow) * (1.0F - lip) *
                    (1.0F - nose * .97F),
          under * face * (1.0F - eye), eye * face, brow * face,
          blush * face, lip * face};
    }
  }
  return map;
}

void blur(const ImageView src, OwnedImage& dst, int radius) {
  OwnedImage temp(src.width, src.height);
  auto tv = temp.view();
  auto dv = dst.view();
  const int diameter = radius * 2 + 1;
  const float inv = 1.0F / static_cast<float>(diameter);
  for (int y = 0; y < src.height; ++y) {
    Color sum{};
    for (int k = -radius; k <= radius; ++k) {
      const Color c = src.get(std::clamp(k, 0, src.width - 1), y);
      sum.r += c.r; sum.g += c.g; sum.b += c.b; sum.a += c.a;
    }
    for (int x = 0; x < src.width; ++x) {
      tv.set(x, y, {sum.r * inv, sum.g * inv, sum.b * inv, sum.a * inv});
      const Color remove = src.get(std::clamp(x - radius, 0, src.width - 1), y);
      const Color add = src.get(std::clamp(x + radius + 1, 0, src.width - 1), y);
      sum.r += add.r - remove.r; sum.g += add.g - remove.g;
      sum.b += add.b - remove.b; sum.a += add.a - remove.a;
    }
  }
  for (int x = 0; x < src.width; ++x) {
    Color sum{};
    for (int k = -radius; k <= radius; ++k) {
      const Color c = tv.get(x, std::clamp(k, 0, src.height - 1));
      sum.r += c.r; sum.g += c.g; sum.b += c.b; sum.a += c.a;
    }
    for (int y = 0; y < src.height; ++y) {
      dv.set(x, y, {sum.r * inv, sum.g * inv, sum.b * inv, sum.a * inv});
      const Color remove = tv.get(x, std::clamp(y - radius, 0, src.height - 1));
      const Color add = tv.get(x, std::clamp(y + radius + 1, 0, src.height - 1));
      sum.r += add.r - remove.r; sum.g += add.g - remove.g;
      sum.b += add.b - remove.b; sum.a += add.a - remove.a;
    }
  }
}

}  // namespace

RetouchMaskGrid retouch_mask_grid(const FaceMesh& mesh, int width, int height) {
  if (width <= 0 || height <= 0 || !mesh.valid())
    throw std::invalid_argument("invalid retouch mask input");
  for (auto p : mesh.points)
    if (!std::isfinite(p.x) || !std::isfinite(p.y) ||
        p.x < 0 || p.x > 1 || p.y < 0 || p.y > 1)
      throw std::invalid_argument("invalid retouch mesh");
  auto m = build_mesh_masks(mesh, width, height);
  return {m.width, m.height, std::move(m.pixels)};
}

MaskSample sample_masks(float nx, float ny, const FaceGeometry& f) {
  const float c = std::cos(-f.roll), s = std::sin(-f.roll);
  const float dx = nx - f.center.x, dy = ny - f.center.y;
  const float x = (c * dx - s * dy) / f.width;
  const float y = (s * dx + c * dy) / f.height;
  const float face = ellipse(x, y, 0.0F, 0.0F, 0.50F, 0.50F, 0.024F);
  const float smoothing_face =
      ellipse(x, y, 0.0F, 0.0F, 0.50F, 0.50F, 0.090F);
  // Protection masks are deliberately wider than the visible feature: this
  // keeps eyelashes, brows, nostrils and the lip contour crisp.
  const float eye_l = ellipse(x, y, -0.18F, -0.11F, 0.18F, 0.13F);
  const float eye_r = ellipse(x, y, 0.18F, -0.11F, 0.18F, 0.13F);
  const float lips = ellipse(x, y, 0.0F, 0.25F, 0.20F, 0.10F);
  const float nose = ellipse(x, y, 0.0F, 0.06F, 0.11F, 0.19F, 0.35F);
  const float under_l = ellipse(x, y, -0.18F, -0.035F, 0.14F, 0.065F);
  const float under_r = ellipse(x, y, 0.18F, -0.035F, 0.14F, 0.065F);
  const float blush_l = ellipse(x, y, -0.28F, 0.08F, 0.15F, 0.11F, 0.45F);
  const float blush_r = ellipse(x, y, 0.28F, 0.08F, 0.15F, 0.11F, 0.45F);
  const float brow_l = ellipse(x, y, -0.18F, -0.24F, 0.17F, 0.045F);
  const float brow_r = ellipse(x, y, 0.18F, -0.24F, 0.17F, 0.045F);
  const float eyes = clamp01(eye_l + eye_r);
  return {face, smoothing_face * (1.0F - eyes) * (1.0F - lips) *
                    (1.0F - nose * 0.72F),
          clamp01(under_l + under_r) * face * (1.0F - eyes), eyes * face,
          clamp01(brow_l + brow_r) * face,
          clamp01(blush_l + blush_r) * face, lips * face};
}

template <typename MaskProvider>
void process_impl(ImageView image, float confidence, const RetouchParams& p,
                  MaskProvider&& masks, const ComplexionParams* complexion = nullptr,
                  const FaceMesh* mesh = nullptr) {
  if (!image.rgba || image.width <= 0 || image.height <= 0 || p.before_after ||
      p.master <= 0.0F || confidence <= 0.0F) return;

  OwnedImage original(image.width, image.height);
  std::copy(image.rgba, image.rgba + static_cast<size_t>(image.stride_floats) *
      static_cast<size_t>(image.height), original.pixels.begin());
  OwnedImage fine(image.width, image.height), coarse(image.width, image.height);
  const int short_side = std::min(image.width, image.height);
  blur(original.view(), fine, std::max(1, short_side / 180));
  blur(original.view(), coarse, std::max(2, short_side / 70));

  const auto ov = original.view();
  const auto fv = fine.view();
  const auto cv = coarse.view();
  const float master = clamp01(p.master) * clamp01(confidence);
  const float fw = mesh ? point_distance(mesh->points[234],mesh->points[454]) : 1.F;
  const float fh = mesh ? point_distance(mesh->points[10],mesh->points[152]) : 1.F;
  const Point chin = mesh ? Point{mesh->points[200].x,
      (mesh->points[200].y+mesh->points[152].y)*.5F} : Point{};
  const float roll = mesh ? std::atan2(mesh->points[454].y-mesh->points[234].y,
                                      mesh->points[454].x-mesh->points[234].x) : 0.F;
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      const float nx = (static_cast<float>(x) + 0.5F) / image.width;
      const float ny = (static_cast<float>(y) + 0.5F) / image.height;
      const MaskSample m = masks(nx, ny);
      if (m.face <= 0.0F) continue;
      const Color src = ov.get(x, y);
      const Color low = cv.get(x, y);
      const Color mid = fv.get(x, y);
      // Facetune behaves closer to detail shrinkage than to a uniform blur:
      // weak texture is attenuated while stronger pores/edges retain most of
      // their amplitude. This avoids the plastic look of a fixed gain.
      const float preserve = clamp01(p.preserve_texture);
      const float fine_magnitude = std::max({
          std::abs(src.r - mid.r), std::abs(src.g - mid.g),
          std::abs(src.b - mid.b)});
      const float mid_magnitude = std::max({
          std::abs(mid.r - low.r), std::abs(mid.g - low.g),
          std::abs(mid.b - low.b)});
      const float fine_floor = 0.075F + preserve * 0.45F;
      const float fine_ceiling = 0.45F + preserve * 0.45F;
      float texture = fine_floor +
          (fine_ceiling - fine_floor) *
          smoothstep(0.006F, 0.035F, fine_magnitude);
      const float mid_floor = 0.085F + preserve * 0.35F;
      const float mid_ceiling = 0.45F + preserve * 0.45F;
      float mid_preserve = mid_floor +
          (mid_ceiling - mid_floor) *
          smoothstep(0.004F, 0.030F, mid_magnitude);
      float cheek_weight=0;
      if(complexion && mesh) {
        const float chin_weight=oriented_ellipse(nx,ny,chin,fw*.42F,fh*.20F,roll,.65F);
        const float reduce=clamp01(complexion->detail_reduction)+
            clamp01(complexion->chin_reduction)*chin_weight;
        texture*=1-clamp01(reduce)*.85F;
        mid_preserve*=1-clamp01(reduce)*.92F;
        cheek_weight=std::max(
            oriented_ellipse(nx,ny,mesh->points[50],fw*.22F,fh*.13F,roll,.80F),
            oriented_ellipse(nx,ny,mesh->points[280],fw*.22F,fh*.13F,roll,.80F))*m.skin;
        // Inner cheeks were already close to the reference. Taper the outer
        // blush correction before it reaches those areas.
        const float inner_cheek=std::max(
            oriented_ellipse(nx,ny,mesh->points[205],fw*.095F,fh*.07F,roll,.80F),
            oriented_ellipse(nx,ny,mesh->points[425],fw*.095F,fh*.07F,roll,.80F));
        cheek_weight*=1-inner_cheek;
      }
      Color smooth{
          low.r + (mid.r - low.r) * mid_preserve + (src.r - mid.r) * texture,
          low.g + (mid.g - low.g) * mid_preserve + (src.g - mid.g) * texture,
          low.b + (mid.b - low.b) * mid_preserve + (src.b - mid.b) * texture,
          src.a};
      const float edge_difference = std::max({
          std::abs(low.r - src.r), std::abs(low.g - src.g),
          std::abs(low.b - src.b)});
      const float contour_edge_guard = 1.0F - smoothstep(
          0.025F, 0.090F, edge_difference);
      const float interior_edge_guard = 1.0F - smoothstep(
          0.080F, 0.250F, std::abs(luma(low) - luma(src)));
      // Use the strict RGB guard only in the inward feather of the face mask.
      // In the interior, keep the calibrated skin smoothing strength.
      const float contour_band =
          1.0F - smoothstep(0.90F, 0.999F, m.face);
      const float edge_guard =
          interior_edge_guard +
          (contour_edge_guard - interior_edge_guard) * contour_band;
      Color out = mix(src, smooth, clamp01(master * clamp01(p.skin) *
                                           m.skin * 2.40F * edge_guard));
      if(complexion && cheek_weight>0) {
        // Redistribute only cheek chroma using the existing coarse layer.
        // Chroma preserves luma; the separate lift is bounded and rolls off
        // before source speculars. No geometry or extra blur pass is applied.
        const float out_y=luma(out),low_y=luma(low);
        const float gate=(1-smoothstep(.72F,.91F,out_y))*cheek_weight*master;
        const float diffusion=clamp01(complexion->blush_diffusion)*gate;
        const float saturation=clamp01(complexion->blush_chroma_reduction)*gate;
        out.r=clamp01(out.r+((low.r-low_y)-(out.r-out_y))*diffusion);
        out.g=clamp01(out.g+((low.g-low_y)-(out.g-out_y))*diffusion);
        out.b=clamp01(out.b+((low.b-low_y)-(out.b-out_y))*diffusion);
        // Reduce the red/green opponent component, retaining the warm yellow
        // component. The vector has zero Rec.709 luma before clipping.
        out.r=clamp01(out.r-saturation*.12F);
        out.g=clamp01(out.g+saturation*.12F*(.2126F/.7152F));
        const float lift=clamp01(complexion->blush_lift)*gate*(8.F/255.F);
        out.r=clamp01(out.r+lift);
        out.g=clamp01(out.g+lift);
        out.b=clamp01(out.b+lift);
      }

      const float lum = luma(out);
      const float shine_gate = smoothstep(0.62F, 0.95F, lum);
      const Color neutral{lum, lum, lum, out.a};
      out = mix(out, neutral, master * clamp01(p.shine) * shine_gate * m.skin * 0.22F);

      const float target = 0.58F;
      const float delta = (target - lum) * 0.04F * master * clamp01(p.tone) * m.skin;
      const float tone_color = master * clamp01(p.tone) * m.skin;
      out.r = clamp01(out.r + delta + tone_color * .010F);
      out.g = clamp01(out.g + delta + tone_color * .012F);
      out.b = clamp01(out.b + delta + tone_color * .013F);

      const float under_gain =
          master * clamp01(p.under_eye) * m.under_eye;
      out.r = clamp01(out.r - under_gain * .055F);
      out.g = clamp01(out.g - under_gain * .055F);
      out.b = clamp01(out.b - under_gain * .055F);
      out = mix(out, p.blush_color, master * clamp01(p.blush) * m.blush * 0.22F);
      const float lip_luma = std::max(luma(p.lip_color), .04F);
      const float out_luma = luma(out);
      const float lip_level = out_luma * .82F;
      const Color lip_tint{clamp01(p.lip_color.r * lip_level / lip_luma),
                           clamp01(p.lip_color.g * lip_level / lip_luma),
                           clamp01(p.lip_color.b * lip_level / lip_luma), out.a};
      const Color lip_neutral{lip_level, lip_level, lip_level, out.a};
      const Color balanced_lip_tint = mix(lip_neutral, lip_tint, .955F);
      out = mix(out, balanced_lip_tint,
                master * clamp01(p.lips) * m.lips * 0.48F);

      const float eye_gain = master * clamp01(p.eyes) * m.eyes;
      const float eye_lum = luma(out);
      const Color eye_neutral{eye_lum, eye_lum, eye_lum, out.a};
      out = mix(out, eye_neutral, eye_gain * 1.82F);
      out.r = clamp01(out.r + (src.r - mid.r) * eye_gain * .70F -
                      eye_gain * .014F);
      out.g = clamp01(out.g + (src.g - mid.g) * eye_gain * .70F -
                      eye_gain * .014F);
      out.b = clamp01(out.b + (src.b - mid.b) * eye_gain * .70F -
                      eye_gain * .010F);
      const float brow_gain = master * clamp01(p.eyes) * m.brows;
      out.r = clamp01(out.r + (src.r - mid.r) * brow_gain * .55F -
                      brow_gain * .018F);
      out.g = clamp01(out.g + (src.g - mid.g) * brow_gain * .55F -
                      brow_gain * .018F);
      out.b = clamp01(out.b + (src.b - mid.b) * brow_gain * .55F -
                      brow_gain * .018F);
      image.set(x, y, out);
    }
  }
}

void process(ImageView image, const FaceGeometry& face, const RetouchParams& p) {
  process_impl(image, face.confidence, p, [&](float x, float y) {
    return sample_masks(x, y, face);
  });
}

void process(ImageView image, const FaceMesh& mesh, const RetouchParams& p) {
  if (!mesh.valid()) return;
  const MeshMaskMap masks = build_mesh_masks(mesh, image.width, image.height);
  process_impl(image, mesh.confidence, p, [&](float x, float y) {
    return masks.sample(x, y);
  });
}

void process_complexion(ImageView image,const FaceMesh& mesh,const RetouchParams& p,
                        const ComplexionParams& input) {
  if(!mesh.valid())return;
  auto c=input;
  auto finite=[](float v){return std::isfinite(v)?clamp01(v):0.F;};
  c.detail_reduction=finite(c.detail_reduction);c.chin_reduction=finite(c.chin_reduction);
  c.blush_diffusion=finite(c.blush_diffusion);c.blush_chroma_reduction=finite(c.blush_chroma_reduction);
  c.blush_lift=finite(c.blush_lift);
  if(c.detail_reduction==0&&c.chin_reduction==0&&c.blush_diffusion==0&&c.blush_chroma_reduction==0&&c.blush_lift==0) {
    process(image,mesh,p);return;
  }
  for(auto q:mesh.points)if(!std::isfinite(q.x)||!std::isfinite(q.y)||q.x<0||q.x>1||q.y<0||q.y>1)return;
  const MeshMaskMap masks=build_mesh_masks(mesh,image.width,image.height);
  process_impl(image,mesh.confidence,p,[&](float x,float y){return masks.sample(x,y);},&c,&mesh);
}

}  // namespace rosto
