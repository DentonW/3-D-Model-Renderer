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

  // Animated scenes only.
  Rig *rig = nullptr;
  std::vector<SkinVertex> skin;  // parallel to verts
  std::vector<MorphMesh> morphs;
};

// Any of these means the scene moves, or can: skinned meshes are posed
// through their skeleton even when the file carries no clips.
bool isAnimated(const aiScene *scene) {
  if (scene->mNumAnimations > 0) return true;
  for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
    if (scene->mMeshes[i]->HasBones() || scene->mMeshes[i]->mNumAnimMeshes > 0) return true;
  }
  return false;
}

// The material's diffuse or base-colour map, loaded once however many
// materials share it. 0 when it has none, or it could not be found.
GLuint materialTexture(const aiMaterial *src, LoadContext &ctx,
                       std::unordered_map<std::string, GLuint> &cache) {
  aiString texPath;
  if (src->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath) != AI_SUCCESS &&
      src->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) != AI_SUCCESS) {
    return 0;
  }
  const std::string key(texPath.C_Str());
  if (key.empty()) return 0;

  const auto hit = cache.find(key);
  if (hit != cache.end()) return hit->second;

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
  return tex;
}

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

    // Maya's Standard Surface and Arnold materials -- the default material in
    // current Maya -- keep their colour in Maya's own FBX properties, which
    // assimp passes through untranslated as "$raw." ones, while the standard
    // slots beside them are left empty or at a default. The shader's colour
    // is the one the artist chose, so it takes over: scaled by the shader's
    // base weight, and converted from the linear space Maya works in.
    aiColor3D mayaColor;
    const bool maya = src->Get("$raw.Maya|baseColor", 0, 0, mayaColor) == AI_SUCCESS;
    if (maya) {
      float weight = 1.0f;
      src->Get("$raw.Maya|base", 0, 0, weight);
      dst.color = fileColor(mayaColor.r * weight, mayaColor.g * weight,
                            mayaColor.b * weight, true);
      dst.hasColor = true;
    } else if (found && !standIn && !black) {
      dst.color = fileColor(color.r, color.g, color.b, ctx.linearColors);
      dst.hasColor = true;
    }

    int twoSided = 0;
    if (src->Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS) {
      dst.twoSided = twoSided != 0;
    }

    if (ctx.opts->render.useTextures) dst.texture = materialTexture(src, ctx, cache);
    // A map wired into a Standard Surface's base colour replaces the colour
    // value, which is left holding whatever it was set to before.
    if (maya && dst.texture) dst.hasColor = false;
  }
}

// Fills in which joints move each vertex of a mesh in an animated scene. A
// rigid mesh follows its node outright; a skinned one takes its four
// strongest bones.
void addSkin(const aiMesh *mesh, int node, LoadContext &ctx) {
  const size_t first = ctx.skin.size();
  ctx.skin.resize(first + mesh->mNumVertices);
  SkinVertex *out = &ctx.skin[first];

  if (!mesh->HasBones()) {
    const auto joint = static_cast<std::uint16_t>(ctx.rig->joint(node));
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
      out[i].joints[0] = joint;
      out[i].weights[0] = 1.0f;
    }
    return;
  }

  for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
    const aiBone *bone = mesh->mBones[b];
    const int boneNode = ctx.rig->findNode(bone->mName.C_Str());
    if (boneNode < 0) continue;
    const auto joint = static_cast<std::uint16_t>(ctx.rig->joint(boneNode, bone->mOffsetMatrix));
    for (unsigned int w = 0; w < bone->mNumWeights; ++w) {
      const aiVertexWeight &vw = bone->mWeights[w];
      if (vw.mVertexId >= mesh->mNumVertices || !(vw.mWeight > 0.0f)) continue;
      SkinVertex &s = out[vw.mVertexId];
      int weakest = 0;
      for (int k = 1; k < 4; ++k) {
        if (s.weights[k] < s.weights[weakest]) weakest = k;
      }
      if (vw.mWeight > s.weights[weakest]) {
        s.joints[weakest] = joint;
        s.weights[weakest] = vw.mWeight;
      }
    }
  }

  // Weights have to sum to one, or the vertex is pulled toward the origin. A
  // vertex no bone claims follows the mesh's own node instead of collapsing.
  for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
    SkinVertex &s = out[i];
    const float sum = s.weights[0] + s.weights[1] + s.weights[2] + s.weights[3];
    if (sum > 0.0f) {
      for (float &w : s.weights) w /= sum;
    } else {
      s.joints[0] = static_cast<std::uint16_t>(ctx.rig->joint(node));
      s.weights[0] = 1.0f;
    }
  }
}

// Keeps a mesh's morph targets as offsets from its base vertices. Assimp
// stores each target as a complete replacement mesh.
void addMorphs(const aiMesh *mesh, int node, GLuint firstVertex, LoadContext &ctx) {
  if (mesh->mNumAnimMeshes == 0) return;
  const unsigned int n = mesh->mNumVertices;

  MorphMesh morph;
  morph.node = node;
  morph.firstVertex = firstVertex;
  morph.base.assign(ctx.verts.begin() + firstVertex, ctx.verts.end());
  for (unsigned int t = 0; t < mesh->mNumAnimMeshes; ++t) {
    const aiAnimMesh *target = mesh->mAnimMeshes[t];
    std::vector<Vec3> positions, normals;
    if (target->HasPositions() && target->mNumVertices == n) {
      positions.resize(n);
      for (unsigned int v = 0; v < n; ++v) {
        const aiVector3D d = target->mVertices[v] - mesh->mVertices[v];
        positions[v] = Vec3{d.x, d.y, d.z};
      }
    }
    if (target->HasNormals() && mesh->HasNormals() && target->mNumVertices == n) {
      normals.resize(n);
      for (unsigned int v = 0; v < n; ++v) {
        const aiVector3D d = target->mNormals[v] - mesh->mNormals[v];
        normals[v] = Vec3{d.x, d.y, d.z};
      }
    }
    morph.positions.push_back(std::move(positions));
    morph.normals.push_back(std::move(normals));
    morph.defaults.push_back(target->mWeight);
  }
  ctx.morphs.push_back(std::move(morph));
}

// Appends one instance of a mesh. In a static scene `xf` is its world
// transform, baked into the vertices here. In an animated one the vertices
// stay in the mesh's own space, `node` names the node carrying it, and `xf`
// -- that node's rest transform -- only decides the winding.
void addMesh(const aiMesh *mesh, const aiMatrix4x4 &xf, int node, LoadContext &ctx) {
  if (mesh->mNumFaces == 0 || mesh->mNumVertices == 0) return;
  const bool animated = ctx.rig != nullptr;

  // Normals go through the cofactor matrix of the upper 3x3, whose rows are
  // the cross products of the transform's rows. It is the inverse transpose
  // scaled by the determinant, so it handles non-uniform scale without an
  // inverse, and normalising strips the scale -- but not the determinant's
  // sign. Under a mirror that sign is negative and would turn every normal
  // inward, so it is multiplied back out. A mirror reverses winding too.
  // (Animated meshes get the same treatment in the vertex shader; a skinned
  // one is taken to be unmirrored at rest.)
  const Vec3 r0{xf.a1, xf.a2, xf.a3};
  const Vec3 r1{xf.b1, xf.b2, xf.b3};
  const Vec3 r2{xf.c1, xf.c2, xf.c3};
  const Vec3 c0 = cross(r1, r2), c1 = cross(r2, r0), c2 = cross(r0, r1);
  const bool mirrored = dot(r0, c0) < 0.0f && !(animated && mesh->HasBones());
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
    Vertex v;
    const aiVector3D p = animated ? mesh->mVertices[i] : xf * mesh->mVertices[i];
    v.pos = Vec3{p.x, p.y, p.z};

    if (mesh->HasNormals()) {
      const Vec3 n{mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z};
      v.nrm = animated ? n : normalize(Vec3{dot(c0, n), dot(c1, n), dot(c2, n)}) * normalSign;
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

    if (!animated) {  // animated bounds come from posing, after the walk
      ctx.lo = ctx.boundsSet ? minVec(ctx.lo, v.pos) : v.pos;
      ctx.hi = ctx.boundsSet ? maxVec(ctx.hi, v.pos) : v.pos;
      ctx.boundsSet = true;
    }
    ctx.verts.push_back(v);
  }

  if (animated) {
    addSkin(mesh, node, ctx);
    addMorphs(mesh, node, base, ctx);
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
    addMesh(ctx.scene->mMeshes[node->mMeshes[i]], xf, -1, ctx);
  }
  for (unsigned int i = 0; i < node->mNumChildren; ++i) {
    walk(node->mChildren[i], xf, ctx);
  }
}

void walkAnimated(const aiNode *node, LoadContext &ctx) {
  const int index = ctx.rig->nodeIndex(node);
  for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
    addMesh(ctx.scene->mMeshes[node->mMeshes[i]], ctx.rig->restTransform(index), index,
            ctx);
  }
  for (unsigned int i = 0; i < node->mNumChildren; ++i) {
    walkAnimated(node->mChildren[i], ctx);
  }
}

// A vertex as the shader will place it, for working out bounds on the CPU.
Vec3 posed(Vec3 p, const SkinVertex &s, const std::vector<float> &rows) {
  float m[12] = {};
  for (int k = 0; k < 4; ++k) {
    if (s.weights[k] == 0.0f) continue;
    const float *r = &rows[static_cast<size_t>(s.joints[k]) * 12];
    for (int i = 0; i < 12; ++i) m[i] += s.weights[k] * r[i];
  }
  return Vec3{m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3],
              m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7],
              m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]};
}

// The ground and the camera frame have to suit the whole animation, not just
// its first frame, so the rest pose and a spread of moments through every
// clip are posed and pooled. Dense meshes are thinned out for this; the
// bounds only need to be close.
void animatedBounds(const Rig &rig, LoadContext &ctx) {
  const std::vector<Vertex> &verts = ctx.verts;
  const size_t stride = std::max<size_t>(1, verts.size() / 20000);

  // Which morph mesh, if any, each vertex belongs to.
  std::vector<int> morphOf;
  if (!ctx.morphs.empty()) {
    morphOf.assign(verts.size(), -1);
    for (size_t m = 0; m < ctx.morphs.size(); ++m) {
      const MorphMesh &mm = ctx.morphs[m];
      std::fill(morphOf.begin() + mm.firstVertex,
                morphOf.begin() + mm.firstVertex + mm.base.size(), static_cast<int>(m));
    }
  }

  std::vector<float> rows;
  std::vector<std::vector<float>> weights(ctx.morphs.size());
  auto pool = [&](int clip, double seconds) {
    rig.pose(clip, seconds, rows);
    for (size_t m = 0; m < ctx.morphs.size(); ++m) {
      weights[m] = ctx.morphs[m].defaults;
      rig.morphWeights(clip, seconds, ctx.morphs[m].node, weights[m]);
    }
    for (size_t v = 0; v < verts.size(); v += stride) {
      Vec3 p = verts[v].pos;
      if (!morphOf.empty() && morphOf[v] >= 0) {
        const MorphMesh &mm = ctx.morphs[morphOf[v]];
        const size_t local = v - mm.firstVertex;
        for (size_t t = 0; t < mm.positions.size(); ++t) {
          const float w = weights[morphOf[v]][t];
          if (w != 0.0f && !mm.positions[t].empty()) p += mm.positions[t][local] * w;
        }
      }
      p = posed(p, ctx.skin[v], rows);
      ctx.lo = ctx.boundsSet ? minVec(ctx.lo, p) : p;
      ctx.hi = ctx.boundsSet ? maxVec(ctx.hi, p) : p;
      ctx.boundsSet = true;
    }
  };

  pool(-1, 0.0);
  const std::vector<Rig::Clip> &clips = rig.clips();
  if (clips.empty()) return;
  const int samples = std::max(4, std::min(32, 128 / static_cast<int>(clips.size())));
  for (size_t c = 0; c < clips.size(); ++c) {
    const double seconds = clips[c].seconds();
    for (int s = 0; s <= samples; ++s) {
      // Stop just short of the end, which would wrap back to the start.
      pool(static_cast<int>(c), seconds * std::min(s / double(samples), 0.9999));
    }
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

  // The scene is read raw first, because the post-processing depends on
  // whether it is animated, and only then processed.
  const aiScene *scene = importer.ReadFile(path.c_str(), 0);
  if (!scene || !scene->mRootNode) {
    error = importer.GetErrorString();
    if (error.empty()) error = "assimp could not read the file";
    return false;
  }
  const bool animated = isAnimated(scene);

  // Assimp hands back texture coordinates in one convention whatever the
  // format -- v = 0 at the bottom of the image -- while the images are
  // uploaded top row first. FlipUVs lines the two up.
  unsigned int flags =
      aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
      aiProcess_GenSmoothNormals | aiProcess_SortByPType | aiProcess_FindDegenerates |
      aiProcess_FindInvalidData | aiProcess_GenUVCoords | aiProcess_TransformUVCoords |
      aiProcess_FlipUVs | aiProcess_RemoveRedundantMaterials |
      aiProcess_ImproveCacheLocality;
  // Merging meshes suits a static scene, but it does not carry morph targets
  // across, so an animated one keeps its meshes as they are -- and has each
  // vertex's bones cut down to the four the shader takes.
  flags |= animated ? aiProcess_LimitBoneWeights : aiProcess_OptimizeMeshes;

  scene = importer.ApplyPostProcessing(flags);
  if (!scene || !scene->mRootNode) {
    error = importer.GetErrorString();
    if (error.empty()) error = "assimp could not process the file";
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

  Rig rig;
  if (animated) {
    rig.build(scene, root);
    ctx.rig = &rig;
    ctx.skin.reserve(vertexGuess);
    walkAnimated(scene->mRootNode, ctx);
  } else {
    walk(scene->mRootNode, root, ctx);
  }

  auto fail = [&](const char *message) {
    for (GLuint tex : ctx.textures) glDeleteTextures(1, &tex);
    error = message;
    return false;
  };
  if (ctx.indices.empty()) return fail("the file loaded, but holds no triangles");
  if (rig.jointCount() > 65536) return fail("more than 65,536 joints; too many to animate");
  if (animated) animatedBounds(rig, ctx);

  // Everything is in hand, so the model on screen can now be replaced.
  release();

  upload(ctx.verts, ctx.skin, ctx.indices, !ctx.morphs.empty());
  indices_ = std::move(ctx.indices);
  draws_ = std::move(ctx.draws);
  materials_ = std::move(ctx.materials);
  ownedTextures_ = std::move(ctx.textures);
  lo_ = ctx.lo;
  hi_ = ctx.hi;
  path_ = path;

  stats_ = ModelStats{};
  if (animated) {
    animated_ = true;
    stats_.joints = rig.jointCount();
    for (const MorphMesh &m : ctx.morphs) stats_.morphTargets += m.defaults.size();
    rig_ = std::move(rig);
    morphs_ = std::move(ctx.morphs);

    const size_t stride = std::max<size_t>(1, ctx.verts.size() / 4096);
    for (size_t v = 0; v < ctx.verts.size(); v += stride) {
      samplePositions_.push_back(ctx.verts[v].pos);
      sampleSkin_.push_back(ctx.skin[v]);
    }

    // One buffer texture, three RGBA32F texels (the rows of an affine
    // matrix) per joint, rewritten every frame.
    glGenBuffers(1, &jointBuffer_);
    glBindBuffer(GL_TEXTURE_BUFFER, jointBuffer_);
    glBufferData(GL_TEXTURE_BUFFER,
                 static_cast<GLsizeiptr>(rig_.jointCount() * 12 * sizeof(float)), nullptr,
                 GL_STREAM_DRAW);
    glGenTextures(1, &jointTex_);
    glBindTexture(GL_TEXTURE_BUFFER, jointTex_);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, jointBuffer_);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
    setPose(-1, 0.0);
  }

  stats_.vertices = ctx.verts.size();
  stats_.triangles = indexCount_ / 3;
  stats_.meshes = ctx.meshCount;
  stats_.materials = materials_.size();
  stats_.textures = ownedTextures_.size();
  stats_.loadSeconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  return true;
}

void Model::setPose(int clip, double seconds) {
  if (!animated_) return;

  rig_.pose(clip, seconds, jointRows_);
  glBindBuffer(GL_TEXTURE_BUFFER, jointBuffer_);
  glBufferSubData(GL_TEXTURE_BUFFER, 0,
                  static_cast<GLsizeiptr>(jointRows_.size() * sizeof(float)),
                  jointRows_.data());
  glBindBuffer(GL_TEXTURE_BUFFER, 0);

  Vec3 sum{0.0f, 0.0f, 0.0f};
  for (size_t i = 0; i < samplePositions_.size(); ++i) {
    sum += posed(samplePositions_[i], sampleSkin_[i], jointRows_);
  }
  if (!samplePositions_.empty()) {
    poseCentre_ = sum * (1.0f / static_cast<float>(samplePositions_.size()));
  }

  for (MorphMesh &m : morphs_) {
    weightScratch_ = m.defaults;
    rig_.morphWeights(clip, seconds, m.node, weightScratch_);
    if (weightScratch_ == m.applied) continue;  // unchanged; nothing to upload
    m.applied = weightScratch_;

    morphScratch_ = m.base;
    for (size_t t = 0; t < weightScratch_.size(); ++t) {
      const float w = weightScratch_[t];
      if (w == 0.0f) continue;
      if (!m.positions[t].empty()) {
        for (size_t v = 0; v < morphScratch_.size(); ++v) {
          morphScratch_[v].pos += m.positions[t][v] * w;
        }
      }
      if (!m.normals[t].empty()) {
        for (size_t v = 0; v < morphScratch_.size(); ++v) {
          morphScratch_[v].nrm += m.normals[t][v] * w;
        }
      }
    }
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(m.firstVertex * sizeof(Vertex)),
                    static_cast<GLsizeiptr>(morphScratch_.size() * sizeof(Vertex)),
                    morphScratch_.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }
}

void Model::upload(const std::vector<Vertex> &verts, const std::vector<SkinVertex> &skin,
                   const std::vector<GLuint> &indices, bool dynamic) {
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glGenBuffers(1, &ebo_);

  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)), verts.data(),
               dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);

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

  if (!skin.empty()) {
    glGenBuffers(1, &skinVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, skinVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(skin.size() * sizeof(SkinVertex)),
                 skin.data(), GL_STATIC_DRAW);
    const GLsizei skinStride = static_cast<GLsizei>(sizeof(SkinVertex));
    // Joint numbers go in as integers; as floats they would be interpolated
    // and rounded like any other attribute.
    glVertexAttribIPointer(4, 4, GL_UNSIGNED_SHORT, skinStride,
                           reinterpret_cast<const void *>(offsetof(SkinVertex, joints)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, skinStride,
                          reinterpret_cast<const void *>(offsetof(SkinVertex, weights)));
    glEnableVertexAttribArray(5);
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
  if (skinVbo_) glDeleteBuffers(1, &skinVbo_);
  if (jointBuffer_) glDeleteBuffers(1, &jointBuffer_);
  if (jointTex_) glDeleteTextures(1, &jointTex_);
  for (GLuint tex : ownedTextures_) glDeleteTextures(1, &tex);

  skinVbo_ = jointBuffer_ = jointTex_ = 0;
  animated_ = false;
  rig_ = Rig();
  morphs_.clear();
  samplePositions_.clear();
  sampleSkin_.clear();
  poseCentre_ = Vec3{0.0f, 0.0f, 0.0f};
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
