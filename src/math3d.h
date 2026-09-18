// Just enough linear algebra for one viewport: a 3-vector and a 4x4 matrix.
//
// The matrices are stored row-major, the way the numpy original wrote them,
// and uploaded with transpose = GL_TRUE. Keeping that convention makes the
// projection and view matrices below line-for-line comparable with the Python.
#pragma once

#include <algorithm>
#include <cmath>

struct Vec2 {
  float x = 0.0f, y = 0.0f;
};

struct Vec3 {
  float x = 0.0f, y = 0.0f, z = 0.0f;

  Vec3() = default;
  Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
  explicit Vec3(float s) : x(s), y(s), z(s) {}
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(float s, Vec3 a) { return a * s; }
inline Vec3 operator*(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
inline Vec3 &operator+=(Vec3 &a, Vec3 b) { return a = a + b; }

inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }

// No fixed threshold: a model measured in kilometres of a thing a few
// millimetres across still has directions worth normalising.
inline Vec3 normalize(Vec3 a) {
  const float n = length(a);
  return n > 0.0f ? a * (1.0f / n) : a;
}

inline Vec3 minVec(Vec3 a, Vec3 b) {
  return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}
inline Vec3 maxVec(Vec3 a, Vec3 b) {
  return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}
inline float maxComponent(Vec3 a) { return std::max(a.x, std::max(a.y, a.z)); }

struct Mat4 {
  // Row-major: m[row * 4 + col].
  float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

  float &at(int row, int col) { return m[row * 4 + col]; }
  float at(int row, int col) const { return m[row * 4 + col]; }
};

inline Mat4 operator*(const Mat4 &a, const Mat4 &b) {
  Mat4 out;
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      float sum = 0.0f;
      for (int k = 0; k < 4; ++k) sum += a.at(r, k) * b.at(k, c);
      out.at(r, c) = sum;
    }
  }
  return out;
}

// The two matrices are built in double and stored down to float: a model in
// millimetres viewed from a few metres away is enough to make the difference
// visible in the depth buffer.
inline Mat4 perspective(double fovDeg, double aspect, double near, double far) {
  const double f = 1.0 / std::tan(fovDeg * 3.14159265358979323846 / 180.0 * 0.5);
  Mat4 out;
  for (int i = 0; i < 16; ++i) out.m[i] = 0.0f;
  out.at(0, 0) = static_cast<float>(f / std::max(aspect, 1e-6));
  out.at(1, 1) = static_cast<float>(f);
  out.at(2, 2) = static_cast<float>((far + near) / (near - far));
  out.at(2, 3) = static_cast<float>(2.0 * far * near / (near - far));
  out.at(3, 2) = -1.0f;
  return out;
}

inline Mat4 lookAt(Vec3 eyeF, Vec3 targetF, Vec3 upF) {
  double eye[3] = {eyeF.x, eyeF.y, eyeF.z};
  double fwd[3] = {targetF.x - eye[0], targetF.y - eye[1], targetF.z - eye[2]};
  double n = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
  n = std::max(n, 1e-12);
  for (double &c : fwd) c /= n;

  double up[3] = {upF.x, upF.y, upF.z};
  // Looking straight down the up axis leaves the cross product undefined, so
  // swing the reference up out of the way.
  if (std::fabs(fwd[0] * up[0] + fwd[1] * up[1] + fwd[2] * up[2]) > 0.999) {
    up[0] = 0.0;
    up[1] = 0.0;
    up[2] = 1.0;
  }

  double side[3] = {fwd[1] * up[2] - fwd[2] * up[1], fwd[2] * up[0] - fwd[0] * up[2],
                    fwd[0] * up[1] - fwd[1] * up[0]};
  n = std::sqrt(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
  n = std::max(n, 1e-12);
  for (double &c : side) c /= n;

  const double upv[3] = {side[1] * fwd[2] - side[2] * fwd[1],
                         side[2] * fwd[0] - side[0] * fwd[2],
                         side[0] * fwd[1] - side[1] * fwd[0]};

  Mat4 out;
  for (int i = 0; i < 3; ++i) {
    out.at(0, i) = static_cast<float>(side[i]);
    out.at(1, i) = static_cast<float>(upv[i]);
    out.at(2, i) = static_cast<float>(-fwd[i]);
  }
  out.at(0, 3) = static_cast<float>(-(side[0] * eye[0] + side[1] * eye[1] + side[2] * eye[2]));
  out.at(1, 3) = static_cast<float>(-(upv[0] * eye[0] + upv[1] * eye[1] + upv[2] * eye[2]));
  out.at(2, 3) = static_cast<float>(fwd[0] * eye[0] + fwd[1] * eye[1] + fwd[2] * eye[2]);
  return out;
}

// Round to a 1/2/5 x 10^n step, for grid spacing that reads as a ruler.
inline float niceStep(float x) {
  if (!(x > 0.0f)) return 1.0f;
  const float mag = std::pow(10.0f, std::floor(std::log10(x)));
  for (float k : {1.0f, 2.0f, 5.0f}) {
    if (x <= k * mag) return k * mag;
  }
  return 10.0f * mag;
}

// The ground grid's spacing for a model with these bounds: a 1/2/5 step of
// about half its footprint, so it reads as a ruler in whatever units the
// file uses.
inline float gridCell(Vec3 lo, Vec3 hi) {
  const float span = std::max(hi.x - lo.x, hi.z - lo.z);
  return niceStep((span > 0.0f ? span : 1.0f) * 0.5f);
}
