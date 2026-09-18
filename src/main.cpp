// Window, input and the event loop.
//
// The interaction is the one the tkinter GUI had: left-drag orbits,
// right-drag pans, the wheel zooms, a double-click reframes, and single keys
// toggle the display options. Frames are drawn on demand rather than in a
// spin loop, so a still viewport costs nothing.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "camera.h"
#include "gl33.h"
#include "model.h"
#include "options.h"
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
};

App &app(GLFWwindow *window) {
  return *static_cast<App *>(glfwGetWindowUserPointer(window));
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
    title = baseNameOf(a.model.path()) + "  -  " +
            withCommas(a.model.stats().triangles) + " tris  -  " + title;
  }
  glfwSetWindowTitle(a.window, title.c_str());
}

void describe(const Model &model) {
  const ModelStats &s = model.stats();
  const Vec3 lo = model.boundsMin(), hi = model.boundsMax();
  std::printf("%s\n", baseNameOf(model.path()).c_str());
  std::printf("  %s triangles / %s vertices / %s meshes / %s materials / %s textures\n",
              withCommas(s.triangles).c_str(), withCommas(s.vertices).c_str(),
              withCommas(s.meshes).c_str(), withCommas(s.materials).c_str(),
              withCommas(s.textures).c_str());
  std::printf("  bounds  x [%.3g, %.3g]  y [%.3g, %.3g]  z [%.3g, %.3g]\n", lo.x, hi.x,
              lo.y, hi.y, lo.z, hi.z);
  std::printf("  read in %.2f s\n", s.loadSeconds);
  std::fflush(stdout);
}

bool loadModel(App &a, const std::string &path) {
  std::string error;
  if (!a.model.load(path, a.opts, error)) {
    std::fprintf(stderr, "could not load %s\n  %s\n", path.c_str(), error.c_str());
    return false;
  }
  describe(a.model);
  a.renderer.onModelChanged(a.model);
  a.camera.fov = a.opts.fov;
  a.camera.frame(a.model.boundsMin(), a.model.boundsMax());
  updateTitle(a);
  a.dirty = true;
  return true;
}

void renderFrame(App &a) {
  int width = 0, height = 0;
  glfwGetFramebufferSize(a.window, &width, &height);
  if (width <= 0 || height <= 0) return;  // minimised
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
      o.wireframe = !o.wireframe;
      break;
    case GLFW_KEY_S:
      o.flatShading = !o.flatShading;
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
  // a forward-compatible context drops wide lines, and the axis gnomon is
  // drawn as wide as the supersampling factor, as it was in the Python.
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

  if (const char *missing = gl33_load(reinterpret_cast<void *(*)(const char *)>(
          glfwGetProcAddress))) {
    std::fprintf(stderr, "this GL context has no %s; OpenGL 3.3 is required\n",
                 missing);
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
      "F frame   W wireframe   S flat shading   G grid   B ground\n"
      "T textures   V vertex colours   C back-face culling\n"
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
    // Nothing moves on its own, so wait for input rather than spinning: an
    // idle viewport should not cost a core or a battery.
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
