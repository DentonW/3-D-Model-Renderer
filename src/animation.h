// Animation: the node hierarchy, the joints the vertex shader blends between,
// and every clip's keyframes, copied out of assimp at load time so the
// imported scene can be freed.
//
// A joint is a node's world transform times an offset. For a bone, the offset
// is assimp's offset matrix, which carries a vertex from the mesh's bind pose
// into the bone's space; for a part that moves rigidly with its node, it is
// the identity. The vertex shader treats the two alike, so skinned characters
// and node-animated machinery go down one path.
#pragma once

#include <assimp/anim.h>
#include <assimp/matrix4x4.h>

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct aiNode;
struct aiScene;

class Rig {
 public:
  struct Clip {
    std::string name;
    double duration = 0.0;  // in ticks
    double ticksPerSecond = 25.0;
    double seconds() const { return duration / ticksPerSecond; }

    struct Channel {
      int node = -1;
      std::vector<aiVectorKey> positions;
      std::vector<aiQuatKey> rotations;
      std::vector<aiVectorKey> scalings;
    };
    std::vector<Channel> channels;

    // Morph-target weights over time, for the meshes under one node.
    struct MorphChannel {
      int node = -1;
      std::vector<double> times;
      std::vector<std::vector<std::pair<unsigned, float>>> keys;  // (target, weight)
    };
    std::vector<MorphChannel> morphs;
  };

  // Flattens the node tree and copies every clip. `root` sits above the
  // scene's root node: the Z-up correction, when there is one.
  void build(const aiScene *scene, const aiMatrix4x4 &root);

  // Moves the whole scene by -offset, to bring one far from the origin back
  // near it (see rebaseFor in model.cpp).
  void rebase(const aiVector3D &offset);

  int nodeIndex(const aiNode *node) const;
  int findNode(const std::string &name) const;
  // A node's world transform with no clip applied.
  const aiMatrix4x4 &restTransform(int node) const { return rest_[node]; }

  // The joint for `node` with `offset`, created on first request. Meshes
  // skinned to one skeleton share its bones, so equal pairs share a joint.
  unsigned joint(int node, const aiMatrix4x4 &offset = aiMatrix4x4());
  size_t jointCount() const { return joints_.size(); }

  const std::vector<Clip> &clips() const { return clips_; }

  // Poses every joint `seconds` into `clip` (-1 for the rest pose), as three
  // rows of an affine matrix per joint -- the layout the shader reads.
  void pose(int clip, double seconds, std::vector<float> &rows) const;

  // Overwrites the weights the clip animates for the morph targets of the
  // meshes under `node`; the others keep whatever the caller put there.
  void morphWeights(int clip, double seconds, int node,
                    std::vector<float> &weights) const;

 private:
  struct Node {
    int parent = -1;
    aiMatrix4x4 local;
  };
  struct Joint {
    int node = -1;
    aiMatrix4x4 offset;
  };

  static double ticksAt(const Clip &clip, double seconds);

  std::vector<Node> nodes_;  // parents always ahead of their children
  std::vector<aiMatrix4x4> rest_;
  std::unordered_map<const aiNode *, int> byPointer_;
  std::unordered_map<std::string, int> byName_;
  std::vector<Joint> joints_;
  std::unordered_map<int, std::vector<unsigned>> jointsOfNode_;
  std::vector<Clip> clips_;
  aiMatrix4x4 root_;
  mutable std::vector<aiMatrix4x4> world_;  // scratch while posing
};
