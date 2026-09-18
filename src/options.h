// Render settings and the command line that sets them.
//
// RenderOptions is a field-for-field port of rockgen.render.RenderOptions,
// with the same defaults, so the two renderers agree on the look. The extra
// members at the end cover what a general model loader needs and a
// single-purpose rock viewer did not.
#pragma once

#include <optional>
#include <string>

#include "math3d.h"

// W steps through these in order.
enum class WireMode {
  Off,
  Overlay,  // dark edges over the shaded surface, as in the rock generator
  Only,     // light edges and no surface
};

struct RenderOptions {
  bool flatShading = false;
  WireMode wireframe = WireMode::Off;
  bool showNormals = false;  // a line out of every triangle, as long as it is large
  bool showGround = true;
  Vec3 backgroundTop{0.16f, 0.17f, 0.20f};
  Vec3 backgroundBottom{0.07f, 0.07f, 0.09f};
  Vec3 groundColor{0.20f, 0.21f, 0.23f};
  bool showGrid = true;
  Vec3 gridColor{0.42f, 0.45f, 0.50f};
  float gridStrength = 0.30f;
  Vec3 defaultAlbedo{0.55f, 0.53f, 0.50f};

  // Three-point setup. A single key light leaves everything facing away from
  // it almost black, which hides half the shape.
  Vec3 keyDir{-0.45f, 0.78f, 0.44f};
  Vec3 keyColor{1.05f, 1.00f, 0.92f};
  Vec3 fillDir{0.68f, 0.16f, -0.58f};
  Vec3 fillColor{0.34f, 0.37f, 0.44f};
  Vec3 skyColor{0.30f, 0.33f, 0.39f};
  Vec3 bounceColor{0.21f, 0.19f, 0.17f};
  float rimStrength = 0.20f;
  float exposure = 1.0f;

  int supersample = 2;         // offscreen scale; 2 means 2x2 samples per pixel
  float gridWidth = 1.0f;      // grid line width in window pixels
  // Grid cell size in metres, with every tenth line stronger. Fixed, rather
  // than fitted to the model, so the grid works as a ruler: it shows how big
  // the model really is.
  double gridSize = 0.1;
  bool cullBackfaces = true;   // off rescues models with inconsistent winding
  bool useTextures = true;
  bool useVertexColors = true; // off shows the material colour underneath
  float alphaCutoff = 0.5f;    // for cut-out materials: foliage, fences, decals
};

struct Options {
  RenderOptions render;
  std::string modelPath;
  int windowWidth = 1280;
  int windowHeight = 800;
  float fov = 38.0f;

  // Only consulted for meshes that arrive without normals. Assimp then
  // smooths across edges below this angle and leaves sharper ones faceted.
  float creaseAngle = 60.0f;

  bool zUp = false;             // rotate a Z-up model into this Y-up world
  std::string screenshot;       // render one frame to this PNG, then exit

  // How long one of the file's units is, in metres. 0 means work it out from
  // the file, where it says, or from its format's convention.
  double unitMetres = 0.0;

  // Starting view in degrees, when given; the camera's own defaults otherwise.
  std::optional<float> yaw;
  std::optional<float> pitch;

  // For animated models: the clip to start on, counting from 1 (0 for the
  // rest pose), and how far into it, in seconds.
  int anim = 1;
  double startTime = 0.0;
};

// Returns false on a bad argument (message in `error`) or on --help, in which
// case `showedHelp` is set and the caller should exit cleanly.
bool parseArgs(int argc, char **argv, Options &out, std::string &error, bool &showedHelp);

void printUsage();
