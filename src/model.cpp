#include "model.h"

#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <assimp/Importer.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <unordered_map>

#include "stb_image.h"

namespace {

std::string directoryOf(const std::string &path) {
  const size_t cut = path.find_last_of("/\\");
  return cut == std::string::npos ? std::string(".") : path.substr(0, cut);
}

std::string baseNameOf(const std::string &path) {
  const size_t cut = path.find_last_of("/\\");
  return cut == std::string::npos ? path : path.substr(cut + 1);
}

std::string extensionOf(const std::string &path) {
  const std::string name = baseNameOf(path);
  const size_t dot = name.find_last_of('.');
  if (dot == std::string::npos) return {};
  std::string ext = name.substr(dot + 1);
  for (char &c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return ext;
}

// The shading works on colours as they are displayed -- the rock generator's
// albedo values were picked by eye, in sRGB -- so colours a format stores as
// linear light are converted on the way in.
float linearToSrgb(float c) {
  c = std::min(std::max(c, 0.0f), 1.0f);
  return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

Vec3 fileColor(float r, float g, float b, bool linear) {
  if (!linear) return Vec3{r, g, b};
  return Vec3{linearToSrgb(r), linearToSrgb(g), linearToSrgb(b)};
}

bool fileExists(const std::string &path) {
  if (path.empty()) return false;
  std::FILE *f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::fclose(f);
  return true;
}

// Texture paths in a model file are whatever the authoring tool wrote:
// absolute paths from another machine, Windows separators, or a name with no
// directory at all. Try the plausible readings before giving up.
std::string resolveTexturePath(const std::string &raw, const std::string &modelDir) {
  std::string cleaned = raw;
  for (char &c : cleaned) {
    if (c == '\\') c = '/';
  }
  const std::string base = baseNameOf(cleaned);
  const std::string candidates[] = {
      modelDir + "/" + cleaned,      cleaned,
      modelDir + "/" + base,         modelDir + "/textures/" + base,
      modelDir + "/Textures/" + base, modelDir + "/maps/" + base,
  };
  for (const std::string &c : candidates) {
    if (fileExists(c)) return c;
  }
  return {};
}

GLuint uploadTexture(const unsigned char *rgba, int w, int h) {
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glBindTexture(GL_TEXTURE_2D, 0);
  return tex;
}

GLuint textureFromMemory(const unsigned char *bytes, size_t size) {
  int w = 0, h = 0, channels = 0;
  unsigned char *pixels =
      stbi_load_from_memory(bytes, static_cast<int>(size), &w, &h, &channels, 4);
  if (!pixels) return 0;
  const GLuint tex = uploadTexture(pixels, w, h);
  stbi_image_free(pixels);
  return tex;
}

GLuint textureFromFile(const std::string &path) {
  int w = 0, h = 0, channels = 0;
  unsigned char *pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
  if (!pixels) return 0;
  const GLuint tex = uploadTexture(pixels, w, h);
  stbi_image_free(pixels);
  return tex;
}

// Textures a file carries inside itself -- glb and fbx routinely do -- come
// through either as an encoded png/jpg blob or as a raw BGRA image.
GLuint textureFromEmbedded(const aiTexture *tex) {
  if (tex->mHeight == 0) {
    return textureFromMemory(reinterpret_cast<const unsigned char *>(tex->pcData),
                             tex->mWidth);
  }
  const size_t count = static_cast<size_t>(tex->mWidth) * tex->mHeight;
  std::vector<unsigned char> rgba(count * 4);
  for (size_t i = 0; i < count; ++i) {
    rgba[i * 4 + 0] = tex->pcData[i].r;
    rgba[i * 4 + 1] = tex->pcData[i].g;
    rgba[i * 4 + 2] = tex->pcData[i].b;
    rgba[i * 4 + 3] = tex->pcData[i].a;
  }
  return uploadTexture(rgba.data(), static_cast<int>(tex->mWidth),
                       static_cast<int>(tex->mHeight));
}

struct LoadContext {
  const aiScene *scene = nullptr;
  const Options *opts = nullptr;
  std::string dir;
  bool linearColors = false;  // glTF: vertex and material colours are linear
  std::vector<Vertex> verts;
  std::vector<GLuint> indices;
  std::vector<DrawRange> draws;
  std::vector<Material> materials;
  std::vector<GLuint> textures;  // every GL texture this load created
  Vec3 lo{0.0f, 0.0f, 0.0f}, hi{0.0f, 0.0f, 0.0f};
  bool boundsSet = false;
  size_t meshCount = 0;
};

void readMaterials(LoadContext &ctx) {
  std::unordered_map<std::string, GLuint> cache;
  ctx.materials.resize(ctx.scene->mNumMaterials);

  for (unsigned int i = 0; i < ctx.scene->mNumMaterials; ++i) {
    const aiMaterial *src = ctx.scene->mMaterials[i];
    Material &dst = ctx.materials[i];

    aiColor4D color;
    bool found = false;
#ifdef AI_MATKEY_BASE_COLOR
    found = src->Get(AI_MATKEY_BASE_COLOR, color) == AI_SUCCESS;
#endif
    if (!found) found = src->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS;

    // Two kinds of colour are not really the file's choice, and meshes under
    // them fall back to the default albedo, as a bare mesh did in the rock
    // generator. Assimp gives every mesh a material, inventing one where the
    // file named none: "DefaultMaterial" in 0.6 grey from most importers, an
    // unnamed white one from PLY and glTF. And exporters write pure black
    // when they cannot translate the real shader -- Maya's Arnold materials
    // leave its OBJ exporter that way -- which, drawn as it stands, turns the
    // whole model into a silhouette.
    aiString name;
    src->Get(AI_MATKEY_NAME, name);
    const bool white = color.r == 1.0f && color.g == 1.0f && color.b == 1.0f;
    const bool black = color.r == 0.0f && color.g == 0.0f && color.b == 0.0f;
    const bool standIn = std::strcmp(name.C_Str(), AI_DEFAULT_MATERIAL_NAME) == 0 ||
                         (name.length == 0 && white);
    if (found && !standIn && !black) {
      dst.color = fileColor(color.r, color.g, color.b, ctx.linearColors);
      dst.hasColor = true;
    }

    int twoSided = 0;
    if (src->Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS) {
      dst.twoSided = twoSided != 0;
    }

    if (!ctx.opts->render.useTextures) continue;

    aiString texPath;
    if (src->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath) != AI_SUCCESS &&
        src->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) != AI_SUCCESS) {
      continue;
    }
    const std::string key(texPath.C_Str());
    if (key.empty()) continue;

    const auto hit = cache.find(key);
    if (hit != cache.end()) {
      dst.texture = hit->second;
      continue;
    }

    GLuint tex = 0;
    if (const aiTexture *embedded = ctx.scene->GetEmbeddedTexture(texPath.C_Str())) {
      tex = textureFromEmbedded(embedded);
    } else {
      const std::string resolved = resolveTexturePath(key, ctx.dir);
      if (!resolved.empty()) tex = textureFromFile(resolved);
    }

    if (tex == 0) {
      std::fprintf(stderr, "  texture not loaded: %s\n", key.c_str());
    } else {
      ctx.textures.push_back(tex);
    }
    cache[key] = tex;
    dst.texture = tex;
  }
}

void addMesh(const aiMesh *mesh, const aiMatrix4x4 &xf, LoadContext &ctx) {
  if (mesh->mNumFaces == 0 || mesh->mNumVertices == 0) return;

  // Normals go through the cofactor matrix of the upper 3x3, whose rows are
  // the cross products of the transform's rows. It is the inverse transpose
  // scaled by the determinant, so it handles non-uniform scale without an
  // inverse, and normalising strips the scale -- but not the determinant's
  // sign. Under a mirror that sign is negative and would turn every normal
  // inward, so it is multiplied back out. A mirror reverses winding too.
  const Vec3 r0{xf.a1, xf.a2, xf.a3};
  const Vec3 r1{xf.b1, xf.b2, xf.b3};
  const Vec3 r2{xf.c1, xf.c2, xf.c3};
  const Vec3 c0 = cross(r1, r2), c1 = cross(r2, r0), c2 = cross(r0, r1);
  const bool mirrored = dot(r0, c0) < 0.0f;
  const float normalSign = mirrored ? -1.0f : 1.0f;

  const GLuint base = static_cast<GLuint>(ctx.verts.size());
  const bool hasUV = mesh->HasTextureCoords(0);

  // A colour layer that is solid black or solid white says nothing --
  // exporters emit both as placeholders -- and taken at its word it would
  // blacken the mesh or hide its material colour, so it is ignored.
  bool hasColors = mesh->HasVertexColors(0);
  if (hasColors) {
    const aiColor4D &first = mesh->mColors[0][0];
    bool placeholder = first.r == first.g && first.g == first.b &&
                       (first.r == 0.0f || first.r == 1.0f);
    for (unsigned int i = 1; placeholder && i < mesh->mNumVertices; ++i) {
      const aiColor4D &c = mesh->mColors[0][i];
      placeholder = c.r == first.r && c.g == first.g && c.b == first.b;
    }
    hasColors = !placeholder;
  }

  for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
    const aiVector3D p = xf * mesh->mVertices[i];
    Vertex v;
    v.pos = Vec3{p.x, p.y, p.z};

    if (mesh->HasNormals()) {
      const Vec3 n{mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z};
      v.nrm = normalize(Vec3{dot(c0, n), dot(c1, n), dot(c2, n)}) * normalSign;
    }
    if (hasColors) {
      const aiColor4D &c = mesh->mColors[0][i];
      v.col = fileColor(c.r, c.g, c.b, ctx.linearColors);
    } else {
      v.col = Vec3{1.0f, 1.0f, 1.0f};
    }
    if (hasUV) {
      v.uv = Vec2{mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y};
    }

    ctx.lo = ctx.boundsSet ? minVec(ctx.lo, v.pos) : v.pos;
    ctx.hi = ctx.boundsSet ? maxVec(ctx.hi, v.pos) : v.pos;
    ctx.boundsSet = true;
    ctx.verts.push_back(v);
  }

  const GLuint firstIndex = static_cast<GLuint>(ctx.indices.size());
  for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
    const aiFace &face = mesh->mFaces[f];
    if (face.mNumIndices != 3) continue;  // triangulation should have seen to this
    if (mirrored) {
      ctx.indices.push_back(base + face.mIndices[0]);
      ctx.indices.push_back(base + face.mIndices[2]);
      ctx.indices.push_back(base + face.mIndices[1]);
    } else {
      ctx.indices.push_back(base + face.mIndices[0]);
      ctx.indices.push_back(base + face.mIndices[1]);
      ctx.indices.push_back(base + face.mIndices[2]);
    }
  }

  const GLuint count = static_cast<GLuint>(ctx.indices.size()) - firstIndex;
  if (count == 0) return;

  DrawRange draw;
  draw.firstIndex = firstIndex;
  draw.indexCount = count;
  draw.material = mesh->mMaterialIndex < ctx.materials.size()
                      ? static_cast<int>(mesh->mMaterialIndex)
                      : -1;

  // Vertex colours, where a mesh has them, are its albedo outright rather
  // than a tint on the material colour. That is how the rock generator used
  // them, and its exporters depend on it: they write the average vertex
  // colour into the material as a stand-in for software that ignores vertex
  // colours, so multiplying the two would darken every rock it exports.
  const Material *mat = draw.material >= 0 ? &ctx.materials[draw.material] : nullptr;
  draw.vertexColors = hasColors;
  if (mat && mat->hasColor) {
    draw.baseColor = mat->color;
  } else if (mat && mat->texture) {
    draw.baseColor = Vec3{1.0f, 1.0f, 1.0f};
  } else {
    draw.baseColor = ctx.opts->render.defaultAlbedo;
  }

  ctx.draws.push_back(draw);
  ++ctx.meshCount;
}

void walk(const aiNode *node, const aiMatrix4x4 &parent, LoadContext &ctx) {
  const aiMatrix4x4 xf = parent * node->mTransformation;
  for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
    addMesh(ctx.scene->mMeshes[node->mMeshes[i]], xf, ctx);
  }
  for (unsigned int i = 0; i < node->mNumChildren; ++i) {
    walk(node->mChildren[i], xf, ctx);
  }
}

}  // namespace

bool Model::load(const std::string &path, const Options &opts, std::string &error) {
  const auto started = std::chrono::steady_clock::now();

  Assimp::Importer importer;
  // Points and lines would otherwise reach the triangle pass; dropping them
  // also clears out whatever FindDegenerates demoted.
  importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE,
                              aiPrimitiveType_POINT | aiPrimitiveType_LINE);
  // Only consulted for meshes that arrive with no normals of their own.
  importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, opts.creaseAngle);

  // Assimp hands back texture coordinates in one convention whatever the
  // format -- v = 0 at the bottom of the image -- while the images are
  // uploaded top row first. FlipUVs lines the two up.
  const unsigned int flags =
      aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
      aiProcess_GenSmoothNormals | aiProcess_SortByPType | aiProcess_FindDegenerates |
      aiProcess_FindInvalidData | aiProcess_GenUVCoords | aiProcess_TransformUVCoords |
      aiProcess_FlipUVs | aiProcess_RemoveRedundantMaterials |
      aiProcess_OptimizeMeshes | aiProcess_ImproveCacheLocality;

  const aiScene *scene = importer.ReadFile(path.c_str(), flags);
  if (!scene || !scene->mRootNode) {
    error = importer.GetErrorString();
    if (error.empty()) error = "assimp could not read the file";
    return false;
  }
  if (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) {
    std::fprintf(stderr, "  warning: assimp reports the scene is incomplete\n");
  }

  LoadContext ctx;
  ctx.scene = scene;
  ctx.opts = &opts;
  ctx.dir = directoryOf(path);
  const std::string ext = extensionOf(path);
  ctx.linearColors = ext == "gltf" || ext == "glb";
  readMaterials(ctx);

  size_t vertexGuess = 0, indexGuess = 0;
  for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
    vertexGuess += scene->mMeshes[i]->mNumVertices;
    indexGuess += static_cast<size_t>(scene->mMeshes[i]->mNumFaces) * 3;
  }
  ctx.verts.reserve(vertexGuess);
  ctx.indices.reserve(indexGuess);

  // A Z-up model is corrected by a rotation at the root, so it flows through
  // the same transform path as everything else -- positions, normals and
  // bounds included.
  aiMatrix4x4 root;
  if (opts.zUp) {
    root = aiMatrix4x4(1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1);
  }
  walk(scene->mRootNode, root, ctx);

  if (ctx.indices.empty()) {
    for (GLuint tex : ctx.textures) glDeleteTextures(1, &tex);
    error = "the file loaded, but holds no triangles";
    return false;
  }

  // Everything is in hand, so the model on screen can now be replaced.
  release();

  upload(ctx.verts, ctx.indices);
  indices_ = std::move(ctx.indices);
  draws_ = std::move(ctx.draws);
  materials_ = std::move(ctx.materials);
  ownedTextures_ = std::move(ctx.textures);
  lo_ = ctx.lo;
  hi_ = ctx.hi;
  path_ = path;

  stats_ = ModelStats{};
  stats_.vertices = ctx.verts.size();
  stats_.triangles = indexCount_ / 3;
  stats_.meshes = ctx.meshCount;
  stats_.materials = materials_.size();
  stats_.textures = ownedTextures_.size();
  stats_.loadSeconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  return true;
}

void Model::upload(const std::vector<Vertex> &verts, const std::vector<GLuint> &indices) {
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glGenBuffers(1, &ebo_);

  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)), verts.data(),
               GL_STATIC_DRAW);

  const GLsizei stride = static_cast<GLsizei>(sizeof(Vertex));
  const struct {
    GLuint location;
    GLint size;
    size_t offset;
  } attribs[] = {
      {0, 3, offsetof(Vertex, pos)},
      {1, 3, offsetof(Vertex, nrm)},
      {2, 3, offsetof(Vertex, col)},
      {3, 2, offsetof(Vertex, uv)},
  };
  for (const auto &a : attribs) {
    glVertexAttribPointer(a.location, a.size, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void *>(a.offset));
    glEnableVertexAttribArray(a.location);
  }

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(indices.size() * sizeof(GLuint)), indices.data(),
               GL_STATIC_DRAW);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

  indexCount_ = indices.size();
}

void Model::ensureEdges() {
  if (edgesBuilt_ || indices_.empty()) return;
  edgesBuilt_ = true;

  // Sorting the three corner pairs of every triangle and dropping duplicates
  // costs one pass and one sort, where a hash set of the same edges costs
  // several times the memory on a dense mesh.
  std::vector<unsigned long long> keys;
  keys.reserve(indices_.size());
  for (size_t i = 0; i + 2 < indices_.size(); i += 3) {
    const GLuint tri[3] = {indices_[i], indices_[i + 1], indices_[i + 2]};
    for (int e = 0; e < 3; ++e) {
      const GLuint a = tri[e], b = tri[(e + 1) % 3];
      const GLuint lo = a < b ? a : b, hi = a < b ? b : a;
      keys.push_back((static_cast<unsigned long long>(lo) << 32) | hi);
    }
  }
  std::sort(keys.begin(), keys.end());
  keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

  std::vector<GLuint> edges;
  edges.reserve(keys.size() * 2);
  for (unsigned long long key : keys) {
    edges.push_back(static_cast<GLuint>(key >> 32));
    edges.push_back(static_cast<GLuint>(key & 0xFFFFFFFFull));
  }

  glGenBuffers(1, &edgeEbo_);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, edgeEbo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(edges.size() * sizeof(GLuint)), edges.data(),
               GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  edgeCount_ = keys.size();

  // The index copy was only kept for this.
  indices_.clear();
  indices_.shrink_to_fit();
}

void Model::release() {
  if (vao_) glDeleteVertexArrays(1, &vao_);
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (ebo_) glDeleteBuffers(1, &ebo_);
  if (edgeEbo_) glDeleteBuffers(1, &edgeEbo_);
  for (GLuint tex : ownedTextures_) glDeleteTextures(1, &tex);

  vao_ = vbo_ = ebo_ = edgeEbo_ = 0;
  indexCount_ = edgeCount_ = 0;
  edgesBuilt_ = false;
  indices_.clear();
  draws_.clear();
  materials_.clear();
  ownedTextures_.clear();
  lo_ = hi_ = Vec3{0.0f, 0.0f, 0.0f};
  stats_ = ModelStats{};
  path_.clear();
}
