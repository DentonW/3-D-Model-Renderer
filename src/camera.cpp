#include "camera.h"

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kOrbitPerPixel = 0.012f;   // radians
constexpr float kPanPerPixel = 0.0022f;    // scaled by distance
constexpr float kZoomIn = 0.88f;
constexpr float kZoomOut = 1.136f;         // 1 / kZoomIn, near enough to undo it
constexpr float kPitchLimit = 1.45f;       // just short of the poles
}  // namespace

Vec3 Camera::eye() const {
  const float cp = std::cos(pitch);
  return target + Vec3{std::sin(yaw) * cp, std::sin(pitch), std::cos(yaw) * cp} * distance;
}

void Camera::basis(Vec3 &right, Vec3 &up, Vec3 &forward) const {
  forward = normalize(target - eye());
  Vec3 worldUp{0.0f, 1.0f, 0.0f};
  if (std::fabs(dot(forward, worldUp)) > 0.999f) worldUp = Vec3{0.0f, 0.0f, 1.0f};
  right = normalize(cross(forward, worldUp));
  up = cross(right, forward);
}

void Camera::orbit(float dxPixels, float dyPixels) {
  yaw -= dxPixels * kOrbitPerPixel;
  pitch = std::max(-kPitchLimit, std::min(kPitchLimit, pitch + dyPixels * kOrbitPerPixel));
  // Keep yaw bounded so a few thousand drags cannot cost it its precision.
  if (yaw > kPi) yaw -= 2.0f * kPi;
  if (yaw < -kPi) yaw += 2.0f * kPi;
}

void Camera::pan(float dxPixels, float dyPixels) {
  Vec3 right, up, forward;
  basis(right, up, forward);
  const float k = distance * kPanPerPixel;
  target = target - right * (dxPixels * k) + up * (dyPixels * k);
}

void Camera::zoom(float ticks) {
  distance *= std::pow(ticks > 0.0f ? kZoomIn : kZoomOut, std::fabs(ticks));
  distance = std::max(1e-4f, std::min(1e6f, distance));
}

void Camera::frame(Vec3 lo, Vec3 hi, float margin) {
  target = (lo + hi) * 0.5f;
  // A model with no extent at all -- a single point, a stray empty node --
  // would otherwise put the eye on top of the target.
  float radius = length(hi - lo) * 0.5f;
  if (!(radius > 0.0f)) radius = 1.0f;
  distance = std::max(radius * margin / std::tan(fov * kPi / 180.0f * 0.5f), 1e-3f);
}
