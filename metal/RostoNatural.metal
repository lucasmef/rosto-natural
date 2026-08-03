#include <metal_stdlib>
using namespace metal;

struct Params {
  float master, skin, preserveTexture, underEye;
  float tone, shine, eyes, blush;
  float lips, confidence;
  float2 faceCenter;
  float2 faceSize;
  float roll;
  float3 blushColor;
  float3 lipColor;
};

static float ellipseMask(float2 p, float2 center, float2 radius, float feather) {
  float d = length((p - center) / radius);
  return 1.0 - smoothstep(1.0 - feather, 1.0, d);
}

kernel void rostoNatural(texture2d<float, access::read> source [[texture(0)]],
                         texture2d<float, access::read> fine [[texture(1)]],
                         texture2d<float, access::read> coarse [[texture(2)]],
                         texture2d<float, access::write> output [[texture(3)]],
                         constant Params& p [[buffer(0)]],
                         uint2 gid [[thread_position_in_grid]]) {
  if (gid.x >= output.get_width() || gid.y >= output.get_height()) return;
  const float2 uv = (float2(gid) + 0.5) /
      float2(output.get_width(), output.get_height());
  const float2 delta = uv - p.faceCenter;
  const float c = cos(-p.roll), s = sin(-p.roll);
  const float2 q = float2(c * delta.x - s * delta.y,
                          s * delta.x + c * delta.y) / p.faceSize;
  const float face = ellipseMask(q, float2(0), float2(.5), .024);
  const float smoothingFace =
      ellipseMask(q, float2(0), float2(.5), .090);
  const float eyeL = ellipseMask(q, float2(-.18, -.11), float2(.18, .13), .20);
  const float eyeR = ellipseMask(q, float2( .18, -.11), float2(.18, .13), .20);
  const float eye = saturate(eyeL + eyeR) * face;
  const float brow = saturate(
      ellipseMask(q, float2(-.18, -.24), float2(.17, .045), .20) +
      ellipseMask(q, float2( .18, -.24), float2(.17, .045), .20)) * face;
  const float lip = ellipseMask(q, float2(0, .25), float2(.20, .10), .20) * face;
  const float nose = ellipseMask(q, float2(0, .06), float2(.15, .23), .35);
  const float under = saturate(
      ellipseMask(q, float2(-.18, -.035), float2(.14, .065), .20) +
      ellipseMask(q, float2( .18, -.035), float2(.14, .065), .20)) *
      face * (1.0 - eye);
  const float blush = saturate(
      ellipseMask(q, float2(-.28, .08), float2(.15, .11), .45) +
      ellipseMask(q, float2( .28, .08), float2(.15, .11), .45)) * face;
  const float skinMask =
      smoothingFace * (1.0 - eye) * (1.0 - lip) * (1.0 - nose * .97);

  const float4 src = source.read(gid);
  const float4 mid = fine.read(gid);
  const float4 low = coarse.read(gid);
  const float amount = saturate(p.master) * saturate(p.confidence);
  const float preserve = saturate(p.preserveTexture);
  const float fineMagnitude = max(
      abs(src.r - mid.r), max(abs(src.g - mid.g), abs(src.b - mid.b)));
  const float midMagnitude = max(
      abs(mid.r - low.r), max(abs(mid.g - low.g), abs(mid.b - low.b)));
  const float fineFloor = .075 + preserve * .45;
  const float fineCeiling = .45 + preserve * .45;
  const float texture = mix(
      fineFloor, fineCeiling, smoothstep(.006, .035, fineMagnitude));
  const float midFloor = .085 + preserve * .35;
  const float midCeiling = .45 + preserve * .45;
  const float midPreserve = mix(
      midFloor, midCeiling, smoothstep(.004, .030, midMagnitude));
  const float3 edgeDelta = abs(low.rgb - src.rgb);
  const float edgeDifference =
      max(edgeDelta.r, max(edgeDelta.g, edgeDelta.b));
  const float contourEdgeGuard =
      1.0 - smoothstep(.025, .090, edgeDifference);
  const float interiorEdgeGuard = 1.0 - smoothstep(
      .080, .250, abs(dot(low.rgb - src.rgb, float3(.2126, .7152, .0722))));
  const float contourBand = 1.0 - smoothstep(.90, .999, face);
  const float edgeGuard =
      mix(interiorEdgeGuard, contourEdgeGuard, contourBand);
  float3 color = mix(src.rgb, low.rgb + (mid.rgb - low.rgb) * midPreserve +
      (src.rgb - mid.rgb) * texture,
      saturate(amount * saturate(p.skin) * skinMask * 2.40 * edgeGuard));
  float lum = dot(color, float3(.2126, .7152, .0722));
  color = mix(color, float3(lum), amount * saturate(p.shine) *
      smoothstep(.62, .95, lum) * skinMask * .22);
  const float toneAmount = amount * saturate(p.tone) * skinMask;
  color += (0.58 - lum) * .04 * toneAmount +
      float3(.010, .012, .013) * toneAmount;
  color -= amount * saturate(p.underEye) * under * .055;
  color = mix(color, p.blushColor, amount * saturate(p.blush) * blush * .22);
  const float lipLuma = max(dot(p.lipColor, float3(.2126, .7152, .0722)), .04);
  const float3 lipTint = saturate(p.lipColor *
      (dot(color, float3(.2126, .7152, .0722)) * .82) / lipLuma);
  const float lipLevel =
      dot(color, float3(.2126, .7152, .0722)) * .82;
  const float3 balancedLipTint = mix(float3(lipLevel), lipTint, .955);
  color = mix(color, balancedLipTint,
      amount * saturate(p.lips) * lip * .48);
  const float eyeGain = amount * saturate(p.eyes) * eye;
  color = mix(color, float3(dot(color, float3(.2126, .7152, .0722))),
              eyeGain * 1.82);
  color += (src.rgb - mid.rgb) * eyeGain * .70 +
      eyeGain * float3(-.014, -.014, -.010);
  const float browGain = amount * saturate(p.eyes) * brow;
  color += (src.rgb - mid.rgb) * browGain * .55 - browGain * .018;
  output.write(float4(saturate(color), src.a), gid);
}
