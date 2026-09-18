// The viewport, ported from rockgen.glview.GLViewport.
//
// Everything is drawn into an offscreen buffer at a multiple of the window
// resolution with a 24-bit depth buffer, then blitted down. That was there to
// work around a 16-bit, non-multisampled context in the Python; it is kept
// because it does the same two jobs anywhere: depth precision on flat faces,
// and antialiased silhouettes, at a quality the window's own pixel format
// cannot be asked for after the fact.
#pragma once

#include <string>
#include <vector>

#include "camera.h"
#include "gl33.h"
#include "model.h"
#include "options.h"

class Renderer {
 public:
  bool init(std::string &error);
  void shutdown();

  // Rebuilds the ground quad and the axis gnomon around a new model.
  void onModelChanged(const Model &model);

  // Draws one frame into the offscreen buffer and resolves it to the window.
  // `width` and `height` are the window's framebuffer size in pixels.
  void draw(Model &model, const Camera &camera, const RenderOptions &opts, int width,
            int height);

  // Reads the frame just drawn back at window resolution, RGB, top row first.
  bool readFrame(std::vector<unsigned char> &rgb, int &width, int &height) const;

  const std::string &glInfo() const { return glInfo_; }
  int supersample() const { return supersample_; }

 private:
  bool buildPrograms(std::string &error);
  bool resizeFramebuffer(int width, int height);

  std::string glInfo_;
  int maxTargetSize_ = 2048;
  int supersample_ = 1;  // what the last frame actually used, after clamping

  GLuint meshProgram_ = 0, groundProgram_ = 0, lineProgram_ = 0, bgProgram_ = 0;

  struct MeshUniforms {
    GLint mvp = -1, flat = -1, hasTex = -1, twoSided = -1, tex = -1;
    GLint vertexColor = -1, baseColor = -1, alphaCutoff = -1;
    GLint eye = -1, keyDir = -1, keyCol = -1, fillDir = -1, fillCol = -1;
    GLint sky = -1, bounce = -1, rim = -1, exposure = -1;
  } meshU_;

  struct GroundUniforms {
    GLint mvp = -1, ground = -1, horizon = -1, gridCol = -1, light = -1;
    GLint centre = -1, span = -1, cell = -1, gridStrength = -1, grid = -1;
  } groundU_;

  GLint lineMvp_ = -1;
  GLint bgTop_ = -1, bgBottom_ = -1;

  GLuint groundVao_ = 0, groundVbo_ = 0;
  GLuint axisVao_ = 0, axisVbo_ = 0;
  GLuint bgVao_ = 0, bgVbo_ = 0;

  GLuint fbo_ = 0, fboColor_ = 0, fboDepth_ = 0;
  int fboWidth_ = 0, fboHeight_ = 0;
  int outWidth_ = 0, outHeight_ = 0;
};
