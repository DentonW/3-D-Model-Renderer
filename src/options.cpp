#include "options.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

bool parseFloat(const char *s, float &out) {
  char *end = nullptr;
  const double v = std::strtod(s, &end);
  if (end == s || *end != '\0') return false;
  out = static_cast<float>(v);
  return true;
}

bool parseInt(const char *s, int &out) {
  char *end = nullptr;
  const long v = std::strtol(s, &end, 10);
  if (end == s || *end != '\0') return false;
  out = static_cast<int>(v);
  return true;
}

bool parseColor(const char *s, Vec3 &out) {
  float c[3];
  const char *p = s;
  for (int i = 0; i < 3; ++i) {
    char *end = nullptr;
    c[i] = static_cast<float>(std::strtod(p, &end));
    if (end == p) return false;
    p = end;
    if (i < 2) {
      if (*p != ',') return false;
      ++p;
    }
  }
  if (*p != '\0') return false;
  out = Vec3{c[0], c[1], c[2]};
  return true;
}

bool parseSize(const char *s, int &w, int &h) {
  char *end = nullptr;
  const long a = std::strtol(s, &end, 10);
  if (end == s || (*end != 'x' && *end != 'X')) return false;
  const char *p = end + 1;
  char *end2 = nullptr;
  const long b = std::strtol(p, &end2, 10);
  if (end2 == p || *end2 != '\0') return false;
  w = static_cast<int>(a);
  h = static_cast<int>(b);
  return w > 0 && h > 0;
}

}  // namespace

void printUsage() {
  std::printf(
      "model-renderer -- load a 3-D model with assimp and render it.\n"
      "\n"
      "usage: model-renderer [options] [model-file]\n"
      "\n"
      "Started with no file, the window opens empty; drop a model onto it to\n"
      "load one. Any format assimp reads will do: obj, fbx, gltf/glb, ply,\n"
      "stl, dae, 3ds, blend and the rest.\n"
      "\n"
      "options:\n"
      "  --size WxH         window size (default 1280x800)\n"
      "  --ss N             supersampling factor, 1-4 (default 2)\n"
      "  --fov DEG          vertical field of view (default 38)\n"
      "  --flat             start with flat shading\n"
      "  --wire             start with the wireframe overlay on\n"
      "  --no-ground        hide the ground plane and axis gnomon\n"
      "  --no-grid          keep the ground, drop the grid lines\n"
      "  --no-cull          draw back faces too, for inconsistent winding\n"
      "  --no-textures      shade with material colours only\n"
      "  --albedo R,G,B     colour for models with no colour of their own\n"
      "                     (default 0.55,0.53,0.50)\n"
      "  --exposure X       overall brightness multiplier (default 1.0)\n"
      "  --crease DEG       smoothing limit for meshes that arrive without\n"
      "                     normals (default 60)\n"
      "  --z-up             rotate a Z-up model into this Y-up world\n"
      "  --screenshot FILE  render one frame to a PNG and exit\n"
      "  --help             this text\n"
      "\n"
      "controls:\n"
      "  left-drag orbit | right-drag pan | wheel zoom | double-click frame\n"
      "  F frame   W wireframe   S flat shading   G grid   B ground\n"
      "  T textures   V vertex colours   C back-face culling\n"
      "  P screenshot   R reload   Esc quit\n");
}

bool parseArgs(int argc, char **argv, Options &out, std::string &error,
               bool &showedHelp) {
  showedHelp = false;
  auto needValue = [&](int &i) -> const char * {
    if (i + 1 >= argc) {
      error = std::string("missing value after ") + argv[i];
      return nullptr;
    }
    return argv[++i];
  };

  for (int i = 1; i < argc; ++i) {
    const char *a = argv[i];
    if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
      printUsage();
      showedHelp = true;
      return false;
    } else if (std::strcmp(a, "--flat") == 0) {
      out.render.flatShading = true;
    } else if (std::strcmp(a, "--wire") == 0) {
      out.render.wireframe = true;
    } else if (std::strcmp(a, "--no-ground") == 0) {
      out.render.showGround = false;
    } else if (std::strcmp(a, "--no-grid") == 0) {
      out.render.showGrid = false;
    } else if (std::strcmp(a, "--no-cull") == 0) {
      out.render.cullBackfaces = false;
    } else if (std::strcmp(a, "--no-textures") == 0) {
      out.render.useTextures = false;
    } else if (std::strcmp(a, "--z-up") == 0) {
      out.zUp = true;
    } else if (std::strcmp(a, "--size") == 0) {
      const char *v = needValue(i);
      if (!v) return false;
      if (!parseSize(v, out.windowWidth, out.windowHeight)) {
        error = std::string("expected WxH, got ") + v;
        return false;
      }
    } else if (std::strcmp(a, "--ss") == 0) {
      const char *v = needValue(i);
      if (!v) return false;
      if (!parseInt(v, out.render.supersample) || out.render.supersample < 1 ||
          out.render.supersample > 4) {
        error = std::string("--ss wants 1-4, got ") + v;
        return false;
      }
    } else if (std::strcmp(a, "--fov") == 0) {
      const char *v = needValue(i);
      if (!v) return false;
      if (!parseFloat(v, out.fov) || out.fov <= 1.0f || out.fov >= 179.0f) {
        error = std::string("--fov wants degrees in (1, 179), got ") + v;
        return false;
      }
    } else if (std::strcmp(a, "--exposure") == 0) {
      const char *v = needValue(i);
      if (!v) return false;
      if (!parseFloat(v, out.render.exposure) || out.render.exposure <= 0.0f) {
        error = std::string("--exposure wants a positive number, got ") + v;
        return false;
      }
    } else if (std::strcmp(a, "--crease") == 0) {
      const char *v = needValue(i);
      if (!v) return false;
      if (!parseFloat(v, out.creaseAngle) || out.creaseAngle < 0.0f ||
          out.creaseAngle > 180.0f) {
        error = std::string("--crease wants degrees in [0, 180], got ") + v;
        return false;
      }
    } else if (std::strcmp(a, "--albedo") == 0) {
      const char *v = needValue(i);
      if (!v) return false;
      if (!parseColor(v, out.render.defaultAlbedo)) {
        error = std::string("expected R,G,B, got ") + v;
        return false;
      }
    } else if (std::strcmp(a, "--screenshot") == 0) {
      const char *v = needValue(i);
      if (!v) return false;
      out.screenshot = v;
    } else if (a[0] == '-' && a[1] != '\0') {
      error = std::string("unknown option ") + a;
      return false;
    } else if (out.modelPath.empty()) {
      out.modelPath = a;
    } else {
      error = std::string("only one model at a time, and there is already ") +
              out.modelPath;
      return false;
    }
  }
  return true;
}
