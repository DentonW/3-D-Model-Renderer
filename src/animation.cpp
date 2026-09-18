#include "animation.h"

#include <assimp/scene.h>

#include <algorithm>
#include <cmath>

namespace {

// Index of the last key at or before `t`, or 0 when `t` precedes them all.
template <typename Key>
size_t keyAt(const std::vector<Key> &keys, double t) {
  const auto after = std::upper_bound(
      keys.begin(), keys.end(), t,
      [](double time, const Key &key) { return time < key.mTime; });
  return after == keys.begin() ? 0 : static_cast<size_t>(after - keys.begin()) - 1;
}

// How far `t` has come from key i toward key i + 1, in [0, 1].
template <typename Key>
float progress(const std::vector<Key> &keys, size_t i, double t) {
  if (i + 1 >= keys.size()) return 0.0f;
  const double span = keys[i + 1].mTime - keys[i].mTime;
  if (!(span > 0.0)) return 0.0f;
  return static_cast<float>(std::min(std::max((t - keys[i].mTime) / span, 0.0), 1.0));
}

aiVector3D sample(const std::vector<aiVectorKey> &keys, double t) {
  const size_t i = keyAt(keys, t);
  const float f = progress(keys, i, t);
  if (f <= 0.0f) return keys[i].mValue;
  return keys[i].mValue + (keys[i + 1].mValue - keys[i].mValue) * f;
}

aiQuaternion sample(const std::vector<aiQuatKey> &keys, double t) {
  const size_t i = keyAt(keys, t);
  const float f = progress(keys, i, t);
  if (f <= 0.0f) return keys[i].mValue;
  aiQuaternion out;
  aiQuaternion::Interpolate(out, keys[i].mValue, keys[i + 1].mValue, f);
  return out.Normalize();
}

}  // namespace

void Rig::build(const aiScene *scene, const aiMatrix4x4 &root) {
  root_ = root;

  // Depth first, parents ahead of children, so that one pass in order turns
  // every local transform into a world transform.
  std::vector<std::pair<const aiNode *, int>> pending{{scene->mRootNode, -1}};
  while (!pending.empty()) {
    const auto [node, parent] = pending.back();
    pending.pop_back();
    const int index = static_cast<int>(nodes_.size());
    nodes_.push_back(Node{parent, node->mTransformation});
    byPointer_[node] = index;
    byName_.emplace(node->mName.C_Str(), index);  // the first of any duplicates wins
    for (unsigned int i = node->mNumChildren; i-- > 0;) {
      pending.emplace_back(node->mChildren[i], index);
    }
  }

  rest_.resize(nodes_.size());
  for (size_t i = 0; i < nodes_.size(); ++i) {
    const int p = nodes_[i].parent;
    rest_[i] = (p < 0 ? root_ : rest_[p]) * nodes_[i].local;
  }

  for (unsigned int a = 0; a < scene->mNumAnimations; ++a) {
    const aiAnimation *src = scene->mAnimations[a];
    Clip clip;
    clip.name = src->mName.length > 0 ? src->mName.C_Str() : "clip " + std::to_string(a + 1);
    // Zero means the file did not say; 25 is what assimp's own viewer assumes.
    clip.ticksPerSecond = src->mTicksPerSecond > 0.0 ? src->mTicksPerSecond : 25.0;
    double lastKey = 0.0;

    for (unsigned int c = 0; c < src->mNumChannels; ++c) {
      const aiNodeAnim *in = src->mChannels[c];
      const int node = findNode(in->mNodeName.C_Str());
      if (node < 0) continue;

      Clip::Channel out;
      out.node = node;
      out.positions.assign(in->mPositionKeys, in->mPositionKeys + in->mNumPositionKeys);
      out.rotations.assign(in->mRotationKeys, in->mRotationKeys + in->mNumRotationKeys);
      out.scalings.assign(in->mScalingKeys, in->mScalingKeys + in->mNumScalingKeys);

      // A channel can animate any subset of the three; the rest hold the
      // node's own values.
      aiVector3D scaling, position;
      aiQuaternion rotation;
      nodes_[node].local.Decompose(scaling, rotation, position);
      if (out.positions.empty()) out.positions.emplace_back(0.0, position);
      if (out.rotations.empty()) out.rotations.emplace_back(0.0, rotation);
      if (out.scalings.empty()) out.scalings.emplace_back(0.0, scaling);

      lastKey = std::max({lastKey, out.positions.back().mTime, out.rotations.back().mTime,
                          out.scalings.back().mTime});
      clip.channels.push_back(std::move(out));
    }

    for (unsigned int m = 0; m < src->mNumMorphMeshChannels; ++m) {
      const aiMeshMorphAnim *in = src->mMorphMeshChannels[m];
      // glTF names the channel after the node holding the meshes; FBX does
      // too, with "*<geometry index>" appended.
      const std::string name = in->mName.C_Str();
      int node = findNode(name);
      const size_t star = name.rfind('*');
      if (node < 0 && star != std::string::npos) node = findNode(name.substr(0, star));
      if (node < 0 || in->mNumKeys == 0) continue;

      Clip::MorphChannel out;
      out.node = node;
      for (unsigned int k = 0; k < in->mNumKeys; ++k) {
        const aiMeshMorphKey &key = in->mKeys[k];
        std::vector<std::pair<unsigned, float>> values;
        for (unsigned int v = 0; v < key.mNumValuesAndWeights; ++v) {
          values.emplace_back(key.mValues[v], static_cast<float>(key.mWeights[v]));
        }
        out.times.push_back(key.mTime);
        out.keys.push_back(std::move(values));
      }
      lastKey = std::max(lastKey, out.times.back());
      clip.morphs.push_back(std::move(out));
    }

    clip.duration = src->mDuration > 0.0 ? src->mDuration : lastKey;
    clips_.push_back(std::move(clip));
  }
}

void Rig::rebase(const aiVector3D &offset) {
  aiMatrix4x4 shift;
  aiMatrix4x4::Translation(-offset, shift);
  root_ = shift * root_;
  for (size_t i = 0; i < nodes_.size(); ++i) {
    const int p = nodes_[i].parent;
    rest_[i] = (p < 0 ? root_ : rest_[p]) * nodes_[i].local;
  }
}

int Rig::nodeIndex(const aiNode *node) const {
  const auto it = byPointer_.find(node);
  return it == byPointer_.end() ? -1 : it->second;
}

int Rig::findNode(const std::string &name) const {
  const auto it = byName_.find(name);
  return it == byName_.end() ? -1 : it->second;
}

unsigned Rig::joint(int node, const aiMatrix4x4 &offset) {
  std::vector<unsigned> &existing = jointsOfNode_[node];
  for (unsigned j : existing) {
    if (joints_[j].offset == offset) return j;
  }
  const unsigned j = static_cast<unsigned>(joints_.size());
  joints_.push_back(Joint{node, offset});
  existing.push_back(j);
  return j;
}

double Rig::ticksAt(const Clip &clip, double seconds) {
  double t = seconds * clip.ticksPerSecond;
  if (clip.duration > 0.0) {
    t = std::fmod(t, clip.duration);
    if (t < 0.0) t += clip.duration;
  }
  return t;
}

void Rig::pose(int clipIndex, double seconds, std::vector<float> &rows) const {
  world_.resize(nodes_.size());
  for (size_t i = 0; i < nodes_.size(); ++i) world_[i] = nodes_[i].local;

  if (clipIndex >= 0 && clipIndex < static_cast<int>(clips_.size())) {
    const Clip &clip = clips_[clipIndex];
    const double t = ticksAt(clip, seconds);
    for (const Clip::Channel &channel : clip.channels) {
      world_[channel.node] = aiMatrix4x4(sample(channel.scalings, t),
                                         sample(channel.rotations, t),
                                         sample(channel.positions, t));
    }
  }

  // Parents come first, so each is already a world transform by the time its
  // children are multiplied onto it.
  for (size_t i = 0; i < nodes_.size(); ++i) {
    const int p = nodes_[i].parent;
    world_[i] = (p < 0 ? root_ : world_[p]) * world_[i];
  }

  rows.resize(joints_.size() * 12);
  for (size_t j = 0; j < joints_.size(); ++j) {
    const aiMatrix4x4 m = world_[joints_[j].node] * joints_[j].offset;
    float *r = &rows[j * 12];
    r[0] = m.a1, r[1] = m.a2, r[2] = m.a3, r[3] = m.a4;
    r[4] = m.b1, r[5] = m.b2, r[6] = m.b3, r[7] = m.b4;
    r[8] = m.c1, r[9] = m.c2, r[10] = m.c3, r[11] = m.c4;
  }
}

void Rig::morphWeights(int clipIndex, double seconds, int node,
                       std::vector<float> &weights) const {
  if (clipIndex < 0 || clipIndex >= static_cast<int>(clips_.size())) return;
  const Clip &clip = clips_[clipIndex];

  for (const Clip::MorphChannel &channel : clip.morphs) {
    if (channel.node != node) continue;
    const double t = ticksAt(clip, seconds);
    const auto after = std::upper_bound(channel.times.begin(), channel.times.end(), t);
    const size_t i =
        after == channel.times.begin() ? 0 : static_cast<size_t>(after - channel.times.begin()) - 1;
    float f = 0.0f;
    if (i + 1 < channel.times.size()) {
      const double span = channel.times[i + 1] - channel.times[i];
      if (span > 0.0) {
        f = static_cast<float>(std::min(std::max((t - channel.times[i]) / span, 0.0), 1.0));
      }
    }

    // Targets a key leaves out keep the weight they came in with.
    std::vector<float> from = weights;
    for (const auto &[target, w] : channel.keys[i]) {
      if (target < from.size()) from[target] = w;
    }
    std::vector<float> to = from;
    if (f > 0.0f) {
      for (const auto &[target, w] : channel.keys[i + 1]) {
        if (target < to.size()) to[target] = w;
      }
    }
    for (size_t k = 0; k < weights.size(); ++k) weights[k] = from[k] + (to[k] - from[k]) * f;
  }
}
