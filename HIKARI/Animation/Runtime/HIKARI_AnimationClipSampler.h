#pragma once

#include <string_view>

#include "Animation/Runtime/HIKARI_AnimationPose.h"

namespace HIKARI {
    class ModelAsset;
    struct AnimationClip;
}

namespace HIKARI::ANIMATION {

    const AnimationClip* ResolveAnimationClip(
        const ModelAsset& model,
        const AnimationClipReference& reference) noexcept;

    AnimationClipReference MakeAnimationClipReference(
        const ModelAsset& model,
        size_t clipIndex);

    void BuildBindPose(
        const ModelAsset& model,
        AnimationLocalPose& outPose);

    bool SampleAnimationClip(
        const ModelAsset& model,
        const AnimationClipReference& reference,
        float timeSeconds,
        bool loop,
        AnimationLocalPose& outPose,
        float* outNormalizedTime = nullptr);

    bool BlendAnimationPoses(
        const AnimationLocalPose& from,
        const AnimationLocalPose& to,
        float weight,
        AnimationLocalPose& outPose);

    // Blend only the node named by maskRootNodeName and its descendants.
    // The model hierarchy is the authority for the subtree; an invalid
    // hierarchy, missing root, or mismatched pose is rejected without
    // modifying outPose.  This remains an animation-runtime primitive so
    // callers do not need to own or duplicate model-node traversal.
    bool BlendAnimationPoseSubtree(
        const ModelAsset& model,
        const AnimationLocalPose& from,
        const AnimationLocalPose& to,
        std::string_view maskRootNodeName,
        float weight,
        AnimationLocalPose& outPose);

} // namespace HIKARI::ANIMATION
