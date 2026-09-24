#pragma once

#include <string_view>

#include "Animation/Runtime/HIKARI_AnimationPose.h"

namespace HIKARI {
class ModelAsset;
struct AnimationClip;
} // namespace HIKARI

namespace HIKARI::ANIMATION {

// Resolved data for a repeatedly sampled masked pose.  The cache is
// owned by the AnimatorComponent that owns the layer; vectors retain
// their capacity across frames and are rebuilt only when the model, clip,
// or mask root changes.
struct AnimationPoseSubtreeCache {
    const ModelAsset* model = nullptr;
    uint64_t modelSourceRevision = 0u;
    const AnimationClip* clip = nullptr;
    int maskRootNode = -1;
    int rootMotionNode = -1;
    std::vector<size_t> subtreeNodes{};
    std::vector<int> rootPath{};
    AnimationLocalPose overlayPose{};
    bool valid = false;

    void Reset() noexcept {
        model = nullptr;
        modelSourceRevision = 0u;
        clip = nullptr;
        maskRootNode = -1;
        rootMotionNode = -1;
        subtreeNodes.clear();
        rootPath.clear();
        overlayPose.nodes.clear();
        valid = false;
    }
};

const AnimationClip* ResolveAnimationClip(const ModelAsset& model, const AnimationClipReference& reference) noexcept;

AnimationClipReference MakeAnimationClipReference(const ModelAsset& model, size_t clipIndex);

void BuildBindPose(const ModelAsset& model, AnimationLocalPose& outPose);

bool SampleAnimationClip(const ModelAsset& model, const AnimationClipReference& reference, float timeSeconds, bool loop,
                         AnimationLocalPose& outPose, float* outNormalizedTime = nullptr);

// Same sampler contract with a clip already resolved by the owning
// Animator.  This avoids repeating clip-id/name lookup on every masked
// upper-body sample while preserving the normal bind-pose fallback.
bool SampleResolvedAnimationClip(const ModelAsset& model, const AnimationClip& clip, float timeSeconds, bool loop,
                                 AnimationLocalPose& outPose, float* outNormalizedTime = nullptr);

bool BlendAnimationPoses(const AnimationLocalPose& from, const AnimationLocalPose& to, float weight,
                         AnimationLocalPose& outPose);

// Blend only the node named by maskRootNodeName and its descendants.
// The model hierarchy is the authority for the subtree; an invalid
// hierarchy, missing root, or mismatched pose is rejected without
// modifying outPose.  This remains an animation-runtime primitive so
// callers do not need to own or duplicate model-node traversal.  A
// model-rotation-aligned root keeps a full-body-authored upper-body pose
// facing as authored when its animated pelvis is replaced by locomotion.
bool BlendAnimationPoseSubtree(
    const ModelAsset& model, const AnimationLocalPose& from, const AnimationLocalPose& to,
    std::string_view maskRootNodeName, float weight, AnimationLocalPose& outPose,
    AnimationSubtreeRootAlignment rootAlignment = AnimationSubtreeRootAlignment::ParentLocal);

bool PrepareAnimationPoseSubtreeCache(const ModelAsset& model, std::string_view maskRootNodeName,
                                      const AnimationClipReference& clipReference, AnimationPoseSubtreeCache& outCache);

// Transactional cached variant used by the animation system.  It retains
// the legacy missing-node/non-finite failure behavior while avoiding a
// full hierarchy walk and the per-call global-matrix vectors.
bool BlendAnimationPoseSubtreeCached(
    const ModelAsset& model, const AnimationLocalPose& from, const AnimationLocalPose& to,
    const AnimationPoseSubtreeCache& cache, float weight, AnimationLocalPose& outPose,
    AnimationSubtreeRootAlignment rootAlignment = AnimationSubtreeRootAlignment::ParentLocal);

// In-place sibling of the transactional API.  All validation and root
// alignment work completes before any node is changed, then the existing
// base pose is modified without allocating/copying a second full pose.
bool ApplyAnimationPoseSubtreeInPlace(
    const ModelAsset& model, AnimationLocalPose& inOutPose, const AnimationLocalPose& to,
    const AnimationPoseSubtreeCache& cache, float weight,
    AnimationSubtreeRootAlignment rootAlignment = AnimationSubtreeRootAlignment::ParentLocal);

} // namespace HIKARI::ANIMATION
