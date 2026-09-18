// The orbit camera, a port of rockgen.render.Camera plus the mouse handling
// that lived in the tkinter GUI. The drag/zoom constants are the ones from
// that GUI, so the feel carries over unchanged.
#pragma once

#include "math3d.h"

struct Camera {
  float yaw = 0.6f;
  float pitch = 0.35f;
  float distance = 3.2f;
  Vec3 target{0.0f, 0.0f, 0.0f};
  float fov = 38.0f;

  Vec3 eye() const;

  // Right / up / forward unit vectors.
  void basis(Vec3 &right, Vec3 &up, Vec3 &forward) const;

  void orbit(float dxPixels, float dyPixels);
  void pan(float dxPixels, float dyPixels);
  void zoom(float ticks);

  // Centre on a bounding box and back off far enough to fit it in frame.
  void frame(Vec3 lo, Vec3 hi, float margin = 1.5f);
};
