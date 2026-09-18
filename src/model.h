// Model loading: assimp in, one interleaved vertex buffer out.
//
// The whole scene is flattened into a single VBO/EBO pair, with node
// transforms baked into the positions, and drawn as a list of index ranges --
// one per (mesh, material). That keeps the draw loop close to the Python
// original, which had exactly one buffer to bind, while still letting each
// material set its own colour and texture.
#pragma once

#include <string>
#include <vector>

#include "gl33.h"
#include "math3d.h"
#include "options.h"

struct Vertex {
  Vec3 pos;
  Vec3 nrm;
  Vec3 col;  // vertex colour in sRGB, or white when the mesh carries none
  Vec2 uv;
};

struct Material {
  Vec3 color{1.0f, 1.0f, 1.0f};
  bool hasColor = false;  // false unless the file itself chose the colour
  GLuint texture = 0;     // 0 when the material has no diffuse map
  bool twoSided = false;
};

struct DrawRange {
  GLuint firstIndex = 0;
  GLuint indexCount = 0;
  int material = -1;  // index into materials, -1 for none
  // The albedo when vertex colours are not in use: the material's colour if
  // the file gave one, white under a texture, the default albedo otherwise.
  Vec3 baseColor{1.0f, 1.0f, 1.0f};
  bool vertexColors = false;  // the mesh carries its own per-vertex colours
};

struct ModelStats {
  size_t vertices = 0;
  size_t triangles = 0;
  size_t meshes = 0;
  size_t materials = 0;
  size_t textures = 0;
  double loadSeconds = 0.0;
};

class Model {
 public:
  ~Model() { release(); }

  Model() = default;
  Model(const Model &) = delete;
  Model &operator=(const Model &) = delete;

  // Reads `path` and uploads it. On failure nothing is changed beyond
  // `error` being set, so a failed drag-and-drop leaves the current model on
  // screen.
  bool load(const std::string &path, const Options &opts, std::string &error);

  void release();

  bool empty() const { return indexCount_ == 0; }

  // The wireframe index buffer costs a sort over every triangle corner, so it
  // is built the first time the overlay is switched on rather than at load.
  void ensureEdges();

  GLuint vao() const { return vao_; }
  GLuint indexBuffer() const { return ebo_; }
  GLuint edgeBuffer() const { return edgeEbo_; }
  GLsizei indexCount() const { return static_cast<GLsizei>(indexCount_); }
  GLsizei edgeIndexCount() const { return static_cast<GLsizei>(edgeCount_ * 2); }

  const std::vector<DrawRange> &draws() const { return draws_; }
  const std::vector<Material> &materials() const { return materials_; }
  Vec3 boundsMin() const { return lo_; }
  Vec3 boundsMax() const { return hi_; }
  const ModelStats &stats() const { return stats_; }
  const std::string &path() const { return path_; }

 private:
  void upload(const std::vector<Vertex> &verts, const std::vector<GLuint> &indices);

  GLuint vao_ = 0, vbo_ = 0, ebo_ = 0, edgeEbo_ = 0;
  size_t indexCount_ = 0;
  size_t edgeCount_ = 0;
  bool edgesBuilt_ = false;
  std::vector<GLuint> indices_;  // kept for the lazy edge build
  std::vector<DrawRange> draws_;
  std::vector<Material> materials_;
  std::vector<GLuint> ownedTextures_;
  Vec3 lo_{0.0f, 0.0f, 0.0f}, hi_{0.0f, 0.0f, 0.0f};
  ModelStats stats_;
  std::string path_;
};
