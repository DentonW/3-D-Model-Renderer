// Window, input and the event loop.
//
// The interaction is the one the tkinter GUI had: left-drag orbits,
// right-drag pans, the wheel zooms, a double-click reframes, and single keys
// toggle the display options. Frames are drawn on demand rather than in a
// spin loop, so a still viewport costs nothing; only a playing animation
// keeps the loop turning.
#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "camera.h"
#include "model.h"
#include "options.h"
#include "platform.h"
#include "renderer.h"
#include "stb_image_write.h"

namespace {

struct App {
  GLFWwindow *window = nullptr;
  Options opts;
  Camera camera;
  Model model;
  Renderer renderer;

  bool dirty = true;
  bool orbiting = false;
  bool panning = false;
  double lastX = 0.0, lastY = 0.0;
  double lastClickTime = -1.0;
  double lastClickX = 0.0, lastClickY = 0.0;

  // Animation playback. clip -1 is the rest pose.
  int clip = -1;
  double animTime = 0.0;  // seconds into the clip
  bool playing = true;
  double lastTick = 0.0;  // glfwGetTime() when animTime last advanced
};

App &app(GLFWwindow *window) {
  return *static_cast<App *>(glfwGetWindowUserPointer(window));
}

// True while there is a clip running that needs new frames.
bool animating(const App &a) {
  return a.model.animated() && a.playing && a.clip >= 0 &&
         a.model.clips()[a.clip].seconds() > 0.0;
}

std::string withCommas(size_t value) {
  std::string digits = std::to_string(value);
  for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3) {
    digits.insert(static_cast<size_t>(i), ",");
  }
  return digits;
}

std::string baseNameOf(const std::string &path) {
  const size_t cut = path.find_last_of("/\\");
  return cut == std::string::npos ? path : path.substr(cut + 1);
}

std::string stemOf(const std::string &path) {
  const std::string name = baseNameOf(path);
  const size_t dot = name.find_last_of('.');
  return dot == std::string::npos ? name : name.substr(0, dot);
}

void updateTitle(App &a, const char *note = nullptr) {
  std::string title = "model-renderer";
  if (note) {
    title = std::string(note) + " - " + title;
  } else if (!a.model.empty()) {
    std::string clip;
    if (a.model.animated()) {
      const auto &clips = a.model.clips();
      clip = a.clip < 0 ? std::string("rest pose")
                        : clips[a.clip].name + " (" + std::to_string(a.clip + 1) + "/" +
                              std::to_string(clips.size()) + ")";
      if (a.clip >= 0 && !a.playing) clip += ", paused";
      clip += "  -  ";
    }
    title = baseNameOf(a.model.path()) + "  -  " +
            withCommas(a.model.stats().triangles) + " tris  -  " + clip + title;
  }
  glfwSetWindowTitle(a.window, title.c_str());
}

// A unit's name, for the ones --units and the file formats deal in.
std::string unitName(double metres) {
  static const struct {
    double metres;
    const char *name;
  } names[] = {{1000.0, "kilometres"}, {1.0, "metres"},      {0.01, "centimetres"},
               {0.001, "millimetres"}, {0.0254, "inches"}, {0.3048, "feet"}};
  for (const auto &n : names) {
    if (std::fabs(metres - n.metres) <= n.metres * 1e-6) return n.name;
  }
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.4g m", metres);
  return buf;
}

// The three sides of the model's box in real units, in whichever of km, m,
// cm or mm suits its longest side: "1.87 x 1.55 x 1.87 m", "24 x 3.2 x 2.9 cm".
std::string realSize(Vec3 size, double unitMetres) {
  const double x = size.x * unitMetres, y = size.y * unitMetres, z = size.z * unitMetres;
  const double longest = std::max({x, y, z});
  double scale = 0.001;
  const char *name = "mm";
  if (longest >= 1000.0) {
    scale = 1000.0, name = "km";
  } else if (longest >= 1.0) {
    scale = 1.0, name = "m";
  } else if (longest >= 0.01) {
    scale = 0.01, name = "cm";
  }
  char buf[128];
  std::snprintf(buf, sizeof(buf), "%.3g x %.3g x %.3g %s", x / scale, y / scale, z / scale,
                name);
  return buf;
}

void describe(const Model &model, const Options &opts) {
  const ModelStats &s = model.stats();
  const Vec3 lo = model.boundsMin(), hi = model.boundsMax();
  std::printf("%s\n", baseNameOf(model.path()).c_str());
  if (opts.zUp || opts.modelRoll != 0.0 || opts.modelPitch != 0.0) {
    std::string turns;
    char buf[64];
    if (opts.zUp) turns += "Z-up converted, ";
    if (opts.modelRoll != 0.0) {
      std::snprintf(buf, sizeof(buf), "rolled %g deg, ", opts.modelRoll);
      turns += buf;
    }
    if (opts.modelPitch != 0.0) {
      std::snprintf(buf, sizeof(buf), "pitched %g deg, ", opts.modelPitch);
      turns += buf;
    }
    turns.resize(turns.size() - 2);
    std::printf("  orientation: %s, then set on the ground\n", turns.c_str());
  }
  std::printf("  %s triangles / %s vertices / %s meshes / %s materials / %s textures\n",
              withCommas(s.triangles).c_str(), withCommas(s.vertices).c_str(),
              withCommas(s.meshes).c_str(), withCommas(s.materials).c_str(),
              withCommas(s.textures).c_str());
  // The real size, which the grid shows too, and where it was put, in the
  // file's own coordinates.
  const Vec3 centre = (lo + hi) * 0.5f + model.origin();
  const Vec3 rest = model.restSize(), reach = hi - lo;
  std::printf("  size %s%s  (file units: %s, %s)\n",
              realSize(rest, model.unitMetres()).c_str(), model.animated() ? " at rest" : "",
              unitName(model.unitMetres()).c_str(), model.unitSource().c_str());
  if (maxComponent(reach - rest) > 0.01f * maxComponent(rest)) {
    std::printf("  moves within %s\n", realSize(reach, model.unitMetres()).c_str());
  }
  std::printf("  centred at (%.6g, %.6g, %.6g) in file units\n", centre.x, centre.y, centre.z);
  if (model.animated()) {
    const auto &clips = model.clips();
    std::printf("  animated: %s joints / %s morph targets / %s clips\n",
                withCommas(s.joints).c_str(), withCommas(s.morphTargets).c_str(),
                withCommas(clips.size()).c_str());
    for (size_t i = 0; i < clips.size(); ++i) {
      std::printf("    %2zu  %-24s %6.2f s\n", i + 1, clips[i].name.c_str(),
                  clips[i].seconds());
    }
  }
  std::printf("  read in %.2f s\n", s.loadSeconds);
  std::fflush(stdout);
}

bool loadModel(App &a, const std::string &path) {
  std::string error;
  if (!a.model.load(path, a.opts, error)) {
    std::fprintf(stderr, "could not load %s\n  %s\n", path.c_str(), error.c_str());
    return false;
  }
  describe(a.model, a.opts);

  // Start on the requested clip, or the first if there are fewer than that.
  const int clips = static_cast<int>(a.model.clips().size());
  a.clip = a.opts.anim == 0 || clips == 0 ? -1 : (a.opts.anim <= clips ? a.opts.anim - 1 : 0);
  a.animTime = a.opts.startTime;
  a.playing = true;
  a.lastTick = glfwGetTime();

  a.renderer.onModelChanged(a.model);
  a.camera.fov = a.opts.fov;
  a.camera.frame(a.model.boundsMin(), a.model.boundsMax());
  updateTitle(a);
  a.dirty = true;
  return true;
}

// Moves to another clip -- `step` of +1 or -1 -- with the rest pose as one
// stop on the way round, and starts it from the beginning.
void switchClip(App &a, int step) {
  const int count = static_cast<int>(a.model.clips().size());
  if (!a.model.animated()) return;
  a.clip = (a.clip + 1 + step + (count + 1)) % (count + 1) - 1;
  a.animTime = 0.0;
  a.playing = true;
  a.lastTick = glfwGetTime();
  updateTitle(a);
  a.dirty = true;
}

void renderFrame(App &a) {
  int width = 0, height = 0;
  glfwGetFramebufferSize(a.window, &width, &height);
  if (width <= 0 || height <= 0) return;  // minimised
  a.model.setPose(a.clip, a.animTime);
  a.renderer.draw(a.model, a.camera, a.opts.render, width, height);
  glfwSwapBuffers(a.window);
  a.dirty = false;
}

// Writes the frame currently in the offscreen buffer, so it must follow a
// draw. Returns the file it wrote, or an empty string.
std::string writeScreenshot(App &a, const std::string &requested) {
  std::vector<unsigned char> rgb;
  int width = 0, height = 0;
  if (!a.renderer.readFrame(rgb, width, height)) return {};

  std::string path = requested;
  if (path.empty()) {
    const std::string stem =
        a.model.empty() ? std::string("viewport") : stemOf(a.model.path());
    for (int i = 1; i < 10000; ++i) {
      char candidate[512];
      std::snprintf(candidate, sizeof(candidate), "%s-%04d.png", stem.c_str(), i);
      std::FILE *existing = std::fopen(candidate, "rb");
      if (!existing) {
        path = candidate;
        break;
      }
      std::fclose(existing);
    }
  }
  if (path.empty()) return {};
  if (!stbi_write_png(path.c_str(), width, height, 3, rgb.data(), width * 3)) return {};
  return path;
}

void onKey(GLFWwindow *window, int key, int, int action, int mods) {
  if (action != GLFW_PRESS) return;
  App &a = app(window);
  RenderOptions &o = a.opts.render;

  switch (key) {
    case GLFW_KEY_ESCAPE:
      glfwSetWindowShouldClose(window, GLFW_TRUE);
      return;
    case GLFW_KEY_Q:
      if (mods & (GLFW_MOD_CONTROL | GLFW_MOD_SUPER)) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
      }
      return;
    case GLFW_KEY_F:
      if (!a.model.empty()) a.camera.frame(a.model.boundsMin(), a.model.boundsMax());
      break;
    case GLFW_KEY_W:
      // Off, then over the surface, then alone, then off again.
      o.wireframe = o.wireframe == WireMode::Off       ? WireMode::Overlay
                    : o.wireframe == WireMode::Overlay ? WireMode::Only
                                                       : WireMode::Off;
      break;
    case GLFW_KEY_S:
      o.flatShading = !o.flatShading;
      break;
    case GLFW_KEY_N:
      o.showNormals = !o.showNormals;
      break;
    case GLFW_KEY_G:
      o.showGrid = !o.showGrid;
      break;
    case GLFW_KEY_B:
      o.showGround = !o.showGround;
      break;
    case GLFW_KEY_T:
      o.useTextures = !o.useTextures;
      break;
    case GLFW_KEY_V:
      o.useVertexColors = !o.useVertexColors;
      break;
    case GLFW_KEY_C:
      o.cullBackfaces = !o.cullBackfaces;
      break;
    case GLFW_KEY_SPACE:
      if (!a.model.animated() || a.clip < 0) return;
      a.playing = !a.playing;
      a.lastTick = glfwGetTime();  // so resuming does not jump ahead
      updateTitle(a);
      break;
    case GLFW_KEY_LEFT_BRACKET:
      switchClip(a, -1);
      return;
    case GLFW_KEY_RIGHT_BRACKET:
      switchClip(a, +1);
      return;
    case GLFW_KEY_P: {
      renderFrame(a);  // make sure the offscreen buffer holds the current view
      const std::string written = writeScreenshot(a, {});
      if (written.empty()) {
        std::fprintf(stderr, "could not write a screenshot\n");
      } else {
        std::printf("wrote %s\n", written.c_str());
        std::fflush(stdout);
      }
      return;
    }
    case GLFW_KEY_R: {
      if (a.model.empty()) return;
      // load() clears the old path on its way through, so take a copy first.
      const std::string path = a.model.path();
      loadModel(a, path);
      return;
    }
    default:
      return;
  }
  a.dirty = true;
}

void onMouseButton(GLFWwindow *window, int button, int action, int) {
  App &a = app(window);
  double x = 0.0, y = 0.0;
  glfwGetCursorPos(window, &x, &y);
  a.lastX = x;
  a.lastY = y;

  const bool down = action == GLFW_PRESS;
  if (button == GLFW_MOUSE_BUTTON_LEFT) {
    a.orbiting = down;
    if (down) {
      // GLFW has no double-click event, so pair up two presses close
      // together in both time and place.
      const double now = glfwGetTime();
      const bool quick = a.lastClickTime > 0.0 && now - a.lastClickTime < 0.4;
      const bool steady = std::fabs(x - a.lastClickX) < 6.0 &&
                          std::fabs(y - a.lastClickY) < 6.0;
      if (quick && steady && !a.model.empty()) {
        a.camera.frame(a.model.boundsMin(), a.model.boundsMax());
        a.dirty = true;
        a.lastClickTime = -1.0;
      } else {
        a.lastClickTime = now;
        a.lastClickX = x;
        a.lastClickY = y;
      }
    }
  } else if (button == GLFW_MOUSE_BUTTON_RIGHT || button == GLFW_MOUSE_BUTTON_MIDDLE) {
    a.panning = down;
  }
}

void onCursorPos(GLFWwindow *window, double x, double y) {
  App &a = app(window);
  const float dx = static_cast<float>(x - a.lastX);
  const float dy = static_cast<float>(y - a.lastY);
  a.lastX = x;
  a.lastY = y;

  if (a.orbiting) {
    a.camera.orbit(dx, dy);
    a.dirty = true;
  } else if (a.panning) {
    a.camera.pan(dx, dy);
    a.dirty = true;
  }
}

void onScroll(GLFWwindow *window, double, double yOffset) {
  App &a = app(window);
  if (yOffset == 0.0) return;
  a.camera.zoom(static_cast<float>(yOffset));
  a.dirty = true;
}

void onDrop(GLFWwindow *window, int count, const char **paths) {
  if (count < 1) return;
  App &a = app(window);
  if (!loadModel(a, paths[0])) {
    updateTitle(a, ("could not load " + baseNameOf(paths[0])).c_str());
  }
}

void onFramebufferSize(GLFWwindow *window, int, int) {
  App &a = app(window);
  a.dirty = true;
  // Windows and macOS both run a modal loop while a window is being dragged
  // by its edge, which would leave the viewport frozen until the mouse came
  // up. Drawing from the callback keeps it live.
  renderFrame(a);
}

void onWindowRefresh(GLFWwindow *window) { renderFrame(app(window)); }

void onGlfwError(int code, const char *description) {
  std::fprintf(stderr, "glfw error %d: %s\n", code, description);
}

}  // namespace

int main(int argc, char **argv) {
  // Paths are UTF-8 throughout: GLFW hands dropped files over that way,
  // assimp expects it, and on Windows the manifest makes the command line
  // and the C library's file functions do the same.
  Utf8Console console;
  App a;
  std::string error;
  bool showedHelp = false;
  if (!parseArgs(argc, argv, a.opts, error, showedHelp)) {
    if (showedHelp) return 0;
    std::fprintf(stderr, "%s\n\nTry --help.\n", error.c_str());
    return 2;
  }
  const bool headless = !a.opts.screenshot.empty();

  glfwSetErrorCallback(onGlfwError);
  if (!glfwInit()) {
    std::fprintf(stderr, "could not initialise glfw\n");
    return 1;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  // macOS only hands out a 3.3+ context this way. Elsewhere it is left off:
  // a forward-compatible context drops wide lines, and the axes (and the
  // lone wireframe and normals) are drawn as wide as the supersampling
  // factor, as the Python drew its axes.
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
  glfwWindowHint(GLFW_SAMPLES, 0);  // the offscreen buffer does the antialiasing
  if (headless) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

  a.window = glfwCreateWindow(a.opts.windowWidth, a.opts.windowHeight,
                              "model-renderer", nullptr, nullptr);
  if (!a.window) {
    std::fprintf(stderr,
                 "could not create an OpenGL 3.3 core window -- the driver may be "
                 "too old, or missing\n");
    glfwTerminate();
    return 1;
  }
  glfwSetWindowUserPointer(a.window, &a);
  glfwMakeContextCurrent(a.window);
  glfwSwapInterval(1);

  // glad resolves every GL 3.3 entry point through GLFW, and returns the
  // context's version, or 0 if it could not load them.
  const int glVersion = gladLoadGL(glfwGetProcAddress);
  if (glVersion == 0 || GLAD_VERSION_MAJOR(glVersion) * 10 + GLAD_VERSION_MINOR(glVersion) < 33) {
    std::fprintf(stderr, "could not load OpenGL 3.3 from this context (it reports %d.%d)\n",
                 GLAD_VERSION_MAJOR(glVersion), GLAD_VERSION_MINOR(glVersion));
    glfwTerminate();
    return 1;
  }

  if (!a.renderer.init(error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    glfwTerminate();
    return 1;
  }
  std::printf("%s\n", a.renderer.glInfo().c_str());
  std::fflush(stdout);  // keep it ahead of any load error on stderr

  // Framing a model moves the target and distance but leaves the angles, so
  // a starting view set here holds for the first model and any dropped later.
  constexpr float kRadians = 3.14159265358979323846f / 180.0f;
  if (a.opts.viewYaw) a.camera.yaw = *a.opts.viewYaw * kRadians;
  if (a.opts.viewPitch) a.camera.pitch = *a.opts.viewPitch * kRadians;

  bool loaded = false;
  if (!a.opts.modelPath.empty()) {
    loaded = loadModel(a, a.opts.modelPath);
    if (!loaded && headless) {
      glfwTerminate();
      return 1;
    }
    if (!loaded) updateTitle(a, "load failed");
  } else if (!headless) {
    std::printf("no model given -- drop one onto the window, or run with --help\n");
  }

  if (headless) {
    renderFrame(a);
    const std::string written = writeScreenshot(a, a.opts.screenshot);
    if (written.empty()) {
      std::fprintf(stderr, "could not write %s\n", a.opts.screenshot.c_str());
      glfwTerminate();
      return 1;
    }
    std::printf("wrote %s\n", written.c_str());
    a.model.release();
    a.renderer.shutdown();
    glfwDestroyWindow(a.window);
    glfwTerminate();
    return 0;
  }

  std::printf(
      "\nleft-drag orbit | right-drag pan | wheel zoom | double-click frame\n"
      "F frame   W wireframe (over surface / alone / off)   S flat shading\n"
      "N normals   G grid   B ground   T textures   V vertex colours\n"
      "C back-face culling\n"
      "Space play/pause   [ ] previous/next animation\n"
      "P screenshot   R reload   Esc quit\n");
  std::fflush(stdout);

  glfwSetKeyCallback(a.window, onKey);
  glfwSetMouseButtonCallback(a.window, onMouseButton);
  glfwSetCursorPosCallback(a.window, onCursorPos);
  glfwSetScrollCallback(a.window, onScroll);
  glfwSetDropCallback(a.window, onDrop);
  glfwSetFramebufferSizeCallback(a.window, onFramebufferSize);
  glfwSetWindowRefreshCallback(a.window, onWindowRefresh);

  while (!glfwWindowShouldClose(a.window)) {
    if (animating(a)) {
      const double now = glfwGetTime();
      a.animTime += now - a.lastTick;
      a.lastTick = now;
      // Keep the clock within the clip, where a double holds its precision.
      const double length = a.model.clips()[a.clip].seconds();
      if (a.animTime >= length) a.animTime = std::fmod(a.animTime, length);
      a.dirty = true;
    }
    // Otherwise nothing moves on its own, so wait for input rather than
    // spinning: an idle viewport should not cost a core or a battery.
    if (a.dirty) {
      renderFrame(a);
      glfwPollEvents();
    } else {
      glfwWaitEvents();
    }
  }

  a.model.release();
  a.renderer.shutdown();
  glfwDestroyWindow(a.window);
  glfwTerminate();
  return 0;
}
