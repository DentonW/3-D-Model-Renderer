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

// `skin` pulls the posing function into the vertex shader, and `light` the
// shared lighting function into the fragment shader. A geometry stage is
// optional.
GLuint link(const char *vertexSource, const char *fragmentSource, bool skin, bool light,
            std::string &error, const char *geometrySource = nullptr) {
  std::vector<const char *> vs{shaders::kHeader};
  if (skin) vs.push_back(shaders::kSkin);
  vs.push_back(vertexSource);
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
  GLuint geometry = 0;
  if (geometrySource) {
    geometry = compile(GL_GEOMETRY_SHADER, {shaders::kHeader, geometrySource}, error);
    if (!geometry) {
      glDeleteShader(vertex);
      glDeleteShader(fragment);
      return 0;
    }
  }

  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  if (geometry) glAttachShader(program, geometry);
  glLinkProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  if (geometry) glDeleteShader(geometry);

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
  meshProgram_ = link(shaders::kMeshVS, shaders::kMeshFS, true, true, error);
  if (!meshProgram_) {
    error = "mesh shader: " + error;
    return false;
  }
  groundProgram_ = link(shaders::kGroundVS, shaders::kGroundFS, false, false, error);
  if (!groundProgram_) {
    error = "ground shader: " + error;
    return false;
  }
  lineProgram_ = link(shaders::kLineVS, shaders::kLineFS, true, false, error);
  if (!lineProgram_) {
    error = "line shader: " + error;
    return false;
  }
  normalsProgram_ = link(shaders::kNormalsVS, shaders::kNormalsFS, true, false, error,
                         shaders::kNormalsGS);
  if (!normalsProgram_) {
    error = "normals shader: " + error;
    return false;
  }
  bgProgram_ = link(shaders::kBackgroundVS, shaders::kBackgroundFS, false, false, error);
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
  groundU_.minorStrength = at(groundProgram_, "uMinorStrength");
  groundU_.majorStrength = at(groundProgram_, "uMajorStrength");
  groundU_.gridWidth = at(groundProgram_, "uGridWidth");
  groundU_.grid = at(groundProgram_, "uGrid");

  meshU_.animated = at(meshProgram_, "uAnimated");
  lineMvp_ = at(lineProgram_, "uMVP");
  lineAnimated_ = at(lineProgram_, "uAnimated");
  lineBrightness_ = at(lineProgram_, "uBrightness");
  normalsMvp_ = at(normalsProgram_, "uMVP");
  normalsAnimated_ = at(normalsProgram_, "uAnimated");
  normalsScale_ = at(normalsProgram_, "uScale");
  normalsColor_ = at(normalsProgram_, "uColor");
  bgTop_ = at(bgProgram_, "uTop");
  bgBottom_ = at(bgProgram_, "uBottom");

  // Joint matrices sit on texture unit 1, clear of the material textures on
  // unit 0: samplers of two types on one unit fail every draw.
  glUseProgram(meshProgram_);
  glUniform1i(at(meshProgram_, "uJoints"), 1);
  glUseProgram(lineProgram_);
  glUniform1i(at(lineProgram_, "uJoints"), 1);
  glUseProgram(normalsProgram_);
  glUniform1i(at(normalsProgram_, "uJoints"), 1);
  glUseProgram(0);
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
  // Wound counter-clockwise seen from above, so it faces up: with back faces
  // culled, the plane disappears once the camera drops below it and the
  // model's underside comes into view.
  const float quad[18] = {
      cx - e, groundY, cz - e, cx - e, groundY, cz + e, cx + e, groundY, cz + e,
      cx - e, groundY, cz - e, cx + e, groundY, cz + e, cx + e, groundY, cz - e,
  };
  glBindBuffer(GL_ARRAY_BUFFER, groundVbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_DYNAMIC_DRAW);

  // The axes stand at the file's own origin, wherever that is -- at a
  // character's feet, in the middle of a rock, off to one side -- and point
  // along +x, +y and +z. Each runs out past the model's box in its direction
  // and then on by a quarter of the model's extent that way (a tenth of its
  // largest, at least), so it always comes out of the model, and never
  // shrinks to nothing on a flat one.
  const Vec3 o = -model.origin();  // the file's origin, in drawn coordinates
  const Vec3 size = hi - lo;
  float largest = maxComponent(size);
  if (!(largest > 0.0f)) largest = 1.0f;
  auto reach = [largest](float toFarSide, float extent) {
    return std::max(toFarSide, 0.0f) + std::max(0.25f * extent, 0.1f * largest);
  };
  const float lx = reach(hi.x - o.x, size.x);
  const float ly = reach(hi.y - o.y, size.y);
  const float lz = reach(hi.z - o.z, size.z);
  const float axis[36] = {
      o.x,      o.y,      o.z,      0.86f, 0.32f, 0.32f,  // x
      o.x + lx, o.y,      o.z,      0.86f, 0.32f, 0.32f,
      o.x,      o.y,      o.z,      0.40f, 0.82f, 0.40f,  // y
      o.x,      o.y + ly, o.z,      0.40f, 0.82f, 0.40f,
      o.x,      o.y,      o.z,      0.36f, 0.55f, 0.92f,  // z
      o.x,      o.y,      o.z + lz, 0.36f, 0.55f, 0.92f,
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
  // Everything scale-dependent here is taken from the model's own size, so
  // the frame looks the same whatever units the file is in.
  float diag = length(hi - lo);
  if (!(diag > 0.0f)) diag = 1.0f;
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
    // The contact shadow sits under the model: the middle of its bounds, or
    // for an animated one, wherever the current pose has taken it.
    if (model.animated()) {
      glUniform2f(groundU_.centre, model.poseCentre().x, model.poseCentre().z);
    } else {
      glUniform2f(groundU_.centre, (lo.x + hi.x) * 0.5f, (lo.z + hi.z) * 0.5f);
    }
    glUniform1f(groundU_.span, span);
    // A fixed size in metres, turned into the file's units: the grid is a
    // ruler, so the model's real size shows against it.
    glUniform1f(groundU_.cell, static_cast<float>(opts.gridSize / model.unitMetres()));
    // Every tenth line is stronger: 1 m lines over the 10 cm ones, by
    // default. The two strengths straddle the Python grid's single one.
    glUniform1f(groundU_.minorStrength, opts.gridStrength * 0.6f);
    glUniform1f(groundU_.majorStrength, opts.gridStrength * 1.6f);
    glUniform1f(groundU_.gridWidth, opts.gridWidth * static_cast<float>(ss));
    glUniform1i(groundU_.grid, opts.showGrid ? 1 : 0);

    // Culled like any other surface, which is what hides it from below. It
    // is also pushed back a hair in depth because the gnomon's x and z axes
    // lie exactly in it: at equal depth, which of the two wins each pixel
    // comes down to rounding, and the lines break up into dashes.
    glEnable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    glBindVertexArray(groundVao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_POLYGON_OFFSET_FILL);
  }

  // An animated model's joint matrices, which both the surface and the
  // wireframe pose it by.
  const bool animated = model.animated();
  if (animated) {
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, model.jointTexture());
    glActiveTexture(GL_TEXTURE0);
  }

  if (!model.empty() && opts.wireframe != WireMode::Only) {
    glUseProgram(meshProgram_);
    glUniform1i(meshU_.animated, animated ? 1 : 0);
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
    if (opts.wireframe == WireMode::Overlay) {
      // Push the fill back so the lines sit on top cleanly.
      glEnable(GL_POLYGON_OFFSET_FILL);
      glPolygonOffset(1.0f, 1.0f);
    }

    glActiveTexture(GL_TEXTURE0);
    bool culling = true;  // matches the state the ground pass leaves
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
    glDisable(GL_POLYGON_OFFSET_FILL);
  }

  if (!model.empty() && opts.wireframe != WireMode::Off) {
    model.ensureEdges();
    if (model.edgeIndexCount() > 0) {
      // Over the surface the lines are dark and half a pixel wide, as the
      // rock generator drew them. Alone they are all there is to see, so they
      // are light and a full window pixel wide.
      const bool alone = opts.wireframe == WireMode::Only;
      const Vec3 color = alone ? Vec3{0.80f, 0.82f, 0.86f} : Vec3{0.08f, 0.09f, 0.11f};
      glUseProgram(lineProgram_);
      glUniform1i(lineAnimated_, animated ? 1 : 0);
      glUniform1f(lineBrightness_, 1.0f);
      glUniformMatrix4fv(lineMvp_, 1, GL_TRUE, mvp.m);
      glBindVertexArray(model.vao());
      // The line shader wants a colour in slot 1, where the mesh layout keeps
      // its normals; a constant attribute covers it without a second vertex
      // array.
      glDisableVertexAttribArray(1);
      glVertexAttrib3f(1, color.x, color.y, color.z);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, model.edgeBuffer());
      glLineWidth(alone ? static_cast<float>(ss) : 1.0f);
      glDrawElements(GL_LINES, model.edgeIndexCount(), GL_UNSIGNED_INT, nullptr);
      glEnableVertexAttribArray(1);
    }
  }

  if (!model.empty() && opts.showNormals) {
    // Every triangle goes through the geometry shader, which turns it into
    // its normal. Lines are not culled, so the back faces' normals are drawn
    // too, and the depth test hides whichever the model stands in front of.
    glUseProgram(normalsProgram_);
    glUniform1i(normalsAnimated_, animated ? 1 : 0);
    glUniformMatrix4fv(normalsMvp_, 1, GL_TRUE, mvp.m);
    glUniform1f(normalsScale_, 1.0f);
    glUniform3f(normalsColor_, 0.35f, 0.85f, 1.0f);
    glBindVertexArray(model.vao());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, model.indexBuffer());
    glLineWidth(static_cast<float>(ss));
    glDrawElements(GL_TRIANGLES, model.indexCount(), GL_UNSIGNED_INT, nullptr);
  }

  if (!model.empty() && opts.showGround) {
    glUseProgram(lineProgram_);
    glUniform1i(lineAnimated_, 0);  // the axes stay where they were put
    glUniformMatrix4fv(lineMvp_, 1, GL_TRUE, mvp.m);
    glBindVertexArray(axisVao_);
    glLineWidth(static_cast<float>(std::max(1, ss)));
    // First, faintly, whatever of them is hidden -- inside the model, or
    // under the ground when the model sits above its origin -- so the origin
    // can always be found. Depth writes are off so the full-strength pass
    // after it is not blocked by the faint one.
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_GREATER);
    glUniform1f(lineBrightness_, 0.4f);
    glDrawArrays(GL_LINES, 0, 6);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glUniform1f(lineBrightness_, 1.0f);
    glDrawArrays(GL_LINES, 0, 6);
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
  if (normalsProgram_) glDeleteProgram(normalsProgram_);
  meshProgram_ = groundProgram_ = lineProgram_ = bgProgram_ = normalsProgram_ = 0;

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
