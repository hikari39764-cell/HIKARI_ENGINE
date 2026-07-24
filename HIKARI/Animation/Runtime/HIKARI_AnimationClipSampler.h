#pragma once

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

} // namespace HIKARI::ANIMATION
