#include "renderer.h"

#include <cstdio>

#include "shaders.h"

namespace {

std::string infoLog(GLuint object, bool isProgram) {
  GLint length = 0;
  if (isProgram) {
    glGetProgramiv(object, GL_INFO_LOG_LENGTH, &length);
  } else {
    glGetShaderiv(object, GL_INFO_LOG_LENGTH, &length);
  }
  if (length <= 1) return {};
  std::string log(static_cast<size_t>(length), '\0');
  GLsizei written = 0;
  if (isProgram) {
    glGetProgramInfoLog(object, length, &written, &log[0]);
  } else {
    glGetShaderInfoLog(object, length, &written, &log[0]);
  }
  log.resize(static_cast<size_t>(written));
  return log;
}

GLuint compile(GLenum stage, const std::vector<const char *> &parts,
                std::string &error) {
  const GLuint shader = glCreateShader(stage);
  glShaderSource(shader, static_cast<GLsizei>(parts.size()), parts.data(), nullptr);
  glCompileShader(shader);

  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    error = infoLog(shader, false);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

// `light` pulls in the shared lighting function; only the mesh shader wants it.
GLuint link(const char *vertexSource, const char *fragmentSource, bool light,
            std::string &error) {
  std::vector<const char *> vs{shaders::kHeader, vertexSource};
  std::vector<const char *> fs{shaders::kHeader};
  if (light) fs.push_back(shaders::kLight);
  fs.push_back(fragmentSource);

  const GLuint vertex = compile(GL_VERTEX_SHADER, vs, error);
  if (!vertex) return 0;
  const GLuint fragment = compile(GL_FRAGMENT_SHADER, fs, error);
  if (!fragment) {
    glDeleteShader(vertex);
    return 0;
  }

  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);

  GLint ok = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    error = infoLog(program, true);
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

const char *glString(GLenum name) {
  const GLubyte *s = glGetString(name);
  return s ? reinterpret_cast<const char *>(s) : "?";
}

void setVec3(GLint location, Vec3 v) { glUniform3f(location, v.x, v.y, v.z); }

}  // namespace

bool Renderer::init(std::string &error) {
  glInfo_ = std::string(glString(GL_RENDERER)) + " / " + glString(GL_VERSION);

  if (!buildPrograms(error)) return false;

  GLint maxTexture = 0, maxRender = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
  glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxRender);
  maxTargetSize_ = std::max(1024, std::min(maxTexture, maxRender));

  glGenVertexArrays(1, &groundVao_);
  glGenBuffers(1, &groundVbo_);
  glBindVertexArray(groundVao_);
  glBindBuffer(GL_ARRAY_BUFFER, groundVbo_);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, nullptr);
  glEnableVertexAttribArray(0);

  // Axis gnomon: position + colour.
  glGenVertexArrays(1, &axisVao_);
  glGenBuffers(1, &axisVbo_);
  glBindVertexArray(axisVao_);
  glBindBuffer(GL_ARRAY_BUFFER, axisVbo_);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, nullptr);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24,
                        reinterpret_cast<const void *>(12));
  glEnableVertexAttribArray(1);

  // One oversized triangle for the background gradient, clipped to the frame.
  const float fullscreen[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  glGenVertexArrays(1, &bgVao_);
  glGenBuffers(1, &bgVbo_);
  glBindVertexArray(bgVao_);
  glBindBuffer(GL_ARRAY_BUFFER, bgVbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(fullscreen), fullscreen, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, nullptr);
  glEnableVertexAttribArray(0);

  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  return true;
}

bool Renderer::buildPrograms(std::string &error) {
  meshProgram_ = link(shaders::kMeshVS, shaders::kMeshFS, true, error);
  if (!meshProgram_) {
    error = "mesh shader: " + error;
    return false;
  }
  groundProgram_ = link(shaders::kGroundVS, shaders::kGroundFS, false, error);
  if (!groundProgram_) {
    error = "ground shader: " + error;
    return false;
  }
  lineProgram_ = link(shaders::kLineVS, shaders::kLineFS, false, error);
  if (!lineProgram_) {
    error = "line shader: " + error;
    return false;
  }
  bgProgram_ = link(shaders::kBackgroundVS, shaders::kBackgroundFS, false, error);
  if (!bgProgram_) {
    error = "background shader: " + error;
    return false;
  }

  auto at = [](GLuint program, const char *name) {
    return glGetUniformLocation(program, name);
  };
  meshU_.mvp = at(meshProgram_, "uMVP");
  meshU_.flat = at(meshProgram_, "uFlat");
  meshU_.hasTex = at(meshProgram_, "uHasTex");
  meshU_.twoSided = at(meshProgram_, "uTwoSided");
  meshU_.tex = at(meshProgram_, "uTex");
  meshU_.vertexColor = at(meshProgram_, "uVertexColor");
  meshU_.baseColor = at(meshProgram_, "uBaseColor");
  meshU_.alphaCutoff = at(meshProgram_, "uAlphaCutoff");
  meshU_.eye = at(meshProgram_, "uEye");
  meshU_.keyDir = at(meshProgram_, "uKeyDir");
  meshU_.keyCol = at(meshProgram_, "uKeyCol");
  meshU_.fillDir = at(meshProgram_, "uFillDir");
  meshU_.fillCol = at(meshProgram_, "uFillCol");
  meshU_.sky = at(meshProgram_, "uSky");
  meshU_.bounce = at(meshProgram_, "uBounce");
  meshU_.rim = at(meshProgram_, "uRim");
  meshU_.exposure = at(meshProgram_, "uExposure");

  groundU_.mvp = at(groundProgram_, "uMVP");
  groundU_.ground = at(groundProgram_, "uGround");
  groundU_.horizon = at(groundProgram_, "uHorizon");
  groundU_.gridCol = at(groundProgram_, "uGridCol");
  groundU_.light = at(groundProgram_, "uLight");
  groundU_.centre = at(groundProgram_, "uCentre");
  groundU_.span = at(groundProgram_, "uSpan");
  groundU_.cell = at(groundProgram_, "uCell");
  groundU_.gridStrength = at(groundProgram_, "uGridStrength");
  groundU_.grid = at(groundProgram_, "uGrid");

  lineMvp_ = at(lineProgram_, "uMVP");
  bgTop_ = at(bgProgram_, "uTop");
  bgBottom_ = at(bgProgram_, "uBottom");
  return true;
}

void Renderer::onModelChanged(const Model &model) {
  const Vec3 lo = model.boundsMin();
  const Vec3 hi = model.boundsMax();

  // The plane sits at the bottom of the model rather than at y = 0: a model
  // that was not authored standing on the origin would otherwise be buried in
  // the ground or hovering over it.
  const float groundY = lo.y;
  float span = std::max(hi.x - lo.x, hi.z - lo.z);
  if (!(span > 0.0f)) span = 1.0f;
  const float cx = (lo.x + hi.x) * 0.5f, cz = (lo.z + hi.z) * 0.5f;
  const float e = span * 60.0f;
  const float quad[18] = {
      cx - e, groundY, cz - e, cx + e, groundY, cz - e, cx + e, groundY, cz + e,
      cx - e, groundY, cz - e, cx + e, groundY, cz + e, cx - e, groundY, cz + e,
  };
  glBindBuffer(GL_ARRAY_BUFFER, groundVbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_DYNAMIC_DRAW);

  const float s = std::max(maxComponent(hi - lo), 1e-6f) * 0.55f;
  const float ox = lo.x, oy = groundY, oz = hi.z;
  const float axis[36] = {
      ox,     oy, oz,     0.86f, 0.32f, 0.32f,  // x
      ox + s, oy, oz,     0.86f, 0.32f, 0.32f,
      ox,     oy, oz,     0.40f, 0.82f, 0.40f,  // y
      ox, oy + s, oz,     0.40f, 0.82f, 0.40f,
      ox,     oy, oz,     0.36f, 0.55f, 0.92f,  // z
      ox,     oy, oz - s, 0.36f, 0.55f, 0.92f,
  };
  glBindBuffer(GL_ARRAY_BUFFER, axisVbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(axis), axis, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

bool Renderer::resizeFramebuffer(int width, int height) {
  if (fbo_ && fboWidth_ == width && fboHeight_ == height) return true;
  if (fbo_) {
    glDeleteFramebuffers(1, &fbo_);
    glDeleteTextures(1, &fboColor_);
    glDeleteRenderbuffers(1, &fboDepth_);
    fbo_ = fboColor_ = fboDepth_ = 0;
  }

  glGenFramebuffers(1, &fbo_);
  glGenTextures(1, &fboColor_);
  glGenRenderbuffers(1, &fboDepth_);

  glBindTexture(GL_TEXTURE_2D, fboColor_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);

  glBindRenderbuffer(GL_RENDERBUFFER, fboDepth_);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);

  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         fboColor_, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                            fboDepth_);
  const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  if (status != GL_FRAMEBUFFER_COMPLETE) {
    std::fprintf(stderr, "incomplete framebuffer (0x%X) at %dx%d\n", status, width,
                 height);
    glDeleteFramebuffers(1, &fbo_);
    glDeleteTextures(1, &fboColor_);
    glDeleteRenderbuffers(1, &fboDepth_);
    fbo_ = fboColor_ = fboDepth_ = 0;
    return false;
  }
  fboWidth_ = width;
  fboHeight_ = height;
  return true;
}

void Renderer::draw(Model &model, const Camera &camera, const RenderOptions &opts,
                    int width, int height) {
  outWidth_ = std::max(1, width);
  outHeight_ = std::max(1, height);

  // A 4x buffer on a 4K window is 8192 pixels across, past what some drivers
  // will allocate, so the factor gives way before the resolution does.
  int ss = std::max(1, opts.supersample);
  while (ss > 1 && (outWidth_ * ss > maxTargetSize_ || outHeight_ * ss > maxTargetSize_)) {
    --ss;
  }
  supersample_ = ss;
  const int w = outWidth_ * ss, h = outHeight_ * ss;
  if (!resizeFramebuffer(w, h)) return;

  Vec3 lo = model.boundsMin(), hi = model.boundsMax();
  if (model.empty()) {
    lo = Vec3{0.0f, 0.0f, 0.0f};
    hi = Vec3{1.0f, 1.0f, 1.0f};
  }
  const Vec3 eye = camera.eye();
  const float diag = std::max(length(hi - lo), 1e-6f);
  const double nearPlane = std::max(camera.distance * 0.002f, diag * 1e-4f);
  const double farPlane = camera.distance + diag * 60.0f;
  const Mat4 mvp = perspective(camera.fov, static_cast<double>(w) / std::max(h, 1),
                               nearPlane, farPlane) *
                   lookAt(eye, camera.target, Vec3{0.0f, 1.0f, 0.0f});

  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glViewport(0, 0, w, h);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Background gradient, behind everything.
  glDisable(GL_DEPTH_TEST);
  glUseProgram(bgProgram_);
  setVec3(bgTop_, opts.backgroundTop);
  setVec3(bgBottom_, opts.backgroundBottom);
  glBindVertexArray(bgVao_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glEnable(GL_DEPTH_TEST);

  const Vec3 keyDir = normalize(opts.keyDir);
  const Vec3 fillDir = normalize(opts.fillDir);

  if (opts.showGround && !model.empty()) {
    float span = std::max(hi.x - lo.x, hi.z - lo.z);
    if (!(span > 0.0f)) span = 1.0f;
    const Vec3 light = opts.keyColor * std::max(keyDir.y, 0.0f) +
                       opts.fillColor * std::max(fillDir.y, 0.0f) + opts.skyColor;

    glUseProgram(groundProgram_);
    glUniformMatrix4fv(groundU_.mvp, 1, GL_TRUE, mvp.m);
    setVec3(groundU_.ground, opts.groundColor);
    setVec3(groundU_.horizon,
            opts.backgroundTop * 0.55f + opts.backgroundBottom * 0.45f);
    setVec3(groundU_.gridCol, opts.gridColor);
    setVec3(groundU_.light, light);
    glUniform2f(groundU_.centre, (lo.x + hi.x) * 0.5f, (lo.z + hi.z) * 0.5f);
    glUniform1f(groundU_.span, span);
    glUniform1f(groundU_.cell, niceStep(std::max(span, 1e-3f) * 0.5f));
    glUniform1f(groundU_.gridStrength, opts.gridStrength);
    glUniform1i(groundU_.grid, opts.showGrid ? 1 : 0);

    // The plane is a flat quad seen from either side. It is pushed back a
    // hair in depth because the gnomon's x and z axes lie exactly in it: at
    // equal depth, which of the two wins each pixel comes down to rounding,
    // and the lines break up into dashes.
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    glBindVertexArray(groundVao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_CULL_FACE);
  }

  if (!model.empty()) {
    glUseProgram(meshProgram_);
    glUniformMatrix4fv(meshU_.mvp, 1, GL_TRUE, mvp.m);
    glUniform1i(meshU_.flat, opts.flatShading ? 1 : 0);
    glUniform1f(meshU_.alphaCutoff, opts.alphaCutoff);
    glUniform1i(meshU_.tex, 0);
    setVec3(meshU_.eye, eye);
    setVec3(meshU_.keyDir, keyDir);
    setVec3(meshU_.keyCol, opts.keyColor);
    setVec3(meshU_.fillDir, fillDir);
    setVec3(meshU_.fillCol, opts.fillColor);
    setVec3(meshU_.sky, opts.skyColor);
    setVec3(meshU_.bounce, opts.bounceColor);
    glUniform1f(meshU_.rim, opts.rimStrength);
    glUniform1f(meshU_.exposure, opts.exposure);

    glBindVertexArray(model.vao());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, model.indexBuffer());
    if (opts.wireframe) {
      // Push the fill back so the lines sit on top cleanly.
      glEnable(GL_POLYGON_OFFSET_FILL);
      glPolygonOffset(1.0f, 1.0f);
    }

    glActiveTexture(GL_TEXTURE0);
    bool culling = true;  // matches the state set at the end of the ground pass
    for (const DrawRange &draw : model.draws()) {
      const Material *material =
          draw.material >= 0 ? &model.materials()[draw.material] : nullptr;
      const GLuint texture =
          (material && opts.useTextures) ? material->texture : 0;
      // A material the file marks two-sided, or a scene the user has told us
      // not to cull, is drawn from both sides -- and shaded from both sides,
      // which is what uTwoSided is for.
      const bool twoSided = !opts.cullBackfaces || (material && material->twoSided);
      if (culling == twoSided) {
        culling = !twoSided;
        if (culling) {
          glEnable(GL_CULL_FACE);
        } else {
          glDisable(GL_CULL_FACE);
        }
      }

      setVec3(meshU_.baseColor, draw.baseColor);
      glUniform1i(meshU_.vertexColor, opts.useVertexColors && draw.vertexColors ? 1 : 0);
      glUniform1i(meshU_.hasTex, texture ? 1 : 0);
      glUniform1i(meshU_.twoSided, twoSided ? 1 : 0);
      glBindTexture(GL_TEXTURE_2D, texture);
      glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(draw.indexCount),
                     GL_UNSIGNED_INT,
                     reinterpret_cast<const void *>(
                         static_cast<size_t>(draw.firstIndex) * sizeof(GLuint)));
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    if (!culling) glEnable(GL_CULL_FACE);

    if (opts.wireframe) {
      glDisable(GL_POLYGON_OFFSET_FILL);
      model.ensureEdges();
      if (model.edgeIndexCount() > 0) {
        glUseProgram(lineProgram_);
        glUniformMatrix4fv(lineMvp_, 1, GL_TRUE, mvp.m);
        // The line shader wants a colour in slot 1, where the mesh layout
        // keeps its normals; a constant attribute covers it without a second
        // vertex array.
        glDisableVertexAttribArray(1);
        glVertexAttrib3f(1, 0.08f, 0.09f, 0.11f);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, model.edgeBuffer());
        glLineWidth(1.0f);
        glDrawElements(GL_LINES, model.edgeIndexCount(), GL_UNSIGNED_INT, nullptr);
        glEnableVertexAttribArray(1);
      }
    }

    if (opts.showGround) {
      glUseProgram(lineProgram_);
      glUniformMatrix4fv(lineMvp_, 1, GL_TRUE, mvp.m);
      glBindVertexArray(axisVao_);
      glLineWidth(static_cast<float>(std::max(1, ss)));
      glDrawArrays(GL_LINES, 0, 6);
    }
  }

  glBindVertexArray(0);
  glUseProgram(0);

  // Resolve the supersampled buffer down to the window.
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glBlitFramebuffer(0, 0, w, h, 0, 0, outWidth_, outHeight_, GL_COLOR_BUFFER_BIT,
                    GL_LINEAR);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool Renderer::readFrame(std::vector<unsigned char> &rgb, int &width,
                         int &height) const {
  if (!fbo_ || fboWidth_ <= 0 || fboHeight_ <= 0) return false;

  std::vector<unsigned char> raw(static_cast<size_t>(fboWidth_) * fboHeight_ * 3);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, fboWidth_, fboHeight_, GL_RGB, GL_UNSIGNED_BYTE, raw.data());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

  // Average each supersampled block down to one output pixel, and flip: GL
  // hands back the bottom row first, an image file wants the top.
  const int k = std::max(1, supersample_);
  width = fboWidth_ / k;
  height = fboHeight_ / k;
  rgb.assign(static_cast<size_t>(width) * height * 3, 0);

  for (int y = 0; y < height; ++y) {
    const int srcY = (height - 1 - y) * k;
    for (int x = 0; x < width; ++x) {
      int sum[3] = {0, 0, 0};
      for (int sy = 0; sy < k; ++sy) {
        const unsigned char *row =
            raw.data() + (static_cast<size_t>(srcY + sy) * fboWidth_ + x * k) * 3;
        for (int sx = 0; sx < k; ++sx) {
          sum[0] += row[sx * 3 + 0];
          sum[1] += row[sx * 3 + 1];
          sum[2] += row[sx * 3 + 2];
        }
      }
      const int n = k * k;
      unsigned char *dst = rgb.data() + (static_cast<size_t>(y) * width + x) * 3;
      dst[0] = static_cast<unsigned char>(sum[0] / n);
      dst[1] = static_cast<unsigned char>(sum[1] / n);
      dst[2] = static_cast<unsigned char>(sum[2] / n);
    }
  }
  return true;
}

void Renderer::shutdown() {
  if (meshProgram_) glDeleteProgram(meshProgram_);
  if (groundProgram_) glDeleteProgram(groundProgram_);
  if (lineProgram_) glDeleteProgram(lineProgram_);
  if (bgProgram_) glDeleteProgram(bgProgram_);
  meshProgram_ = groundProgram_ = lineProgram_ = bgProgram_ = 0;

  if (groundVao_) glDeleteVertexArrays(1, &groundVao_);
  if (axisVao_) glDeleteVertexArrays(1, &axisVao_);
  if (bgVao_) glDeleteVertexArrays(1, &bgVao_);
  if (groundVbo_) glDeleteBuffers(1, &groundVbo_);
  if (axisVbo_) glDeleteBuffers(1, &axisVbo_);
  if (bgVbo_) glDeleteBuffers(1, &bgVbo_);
  groundVao_ = axisVao_ = bgVao_ = groundVbo_ = axisVbo_ = bgVbo_ = 0;

  if (fbo_) {
    glDeleteFramebuffers(1, &fbo_);
    glDeleteTextures(1, &fboColor_);
    glDeleteRenderbuffers(1, &fboDepth_);
    fbo_ = fboColor_ = fboDepth_ = 0;
  }
}
