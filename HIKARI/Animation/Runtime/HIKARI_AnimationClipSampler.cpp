#include "Animation/Runtime/HIKARI_AnimationClipSampler.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Animation/Runtime/HIKARI_AnimationPoseEvaluation.h"

namespace HIKARI::ANIMATION {
namespace {
float ClampUnit(float value) noexcept { return std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f); }

float ResolveSampleTime(float timeSeconds, bool loop, float durationSeconds) noexcept {
    float result = std::isfinite(timeSeconds) ? timeSeconds : 0.0f;
    if (durationSeconds <= 1.0e-5f) {
        return 0.0f;
    }
    if (loop) {
        result = std::fmod(result, durationSeconds);
        if (result < 0.0f) {
            result += durationSeconds;
        }
        return result;
    }
    return std::clamp(result, 0.0f, durationSeconds);
}

MATH::Vec3 Lerp(const MATH::Vec3& from, const MATH::Vec3& to, float weight) noexcept {
    return from + (to - from) * weight;
}

MATH::Vec3 Hermite(const MATH::Vec3& from, const MATH::Vec3& fromTangent, const MATH::Vec3& to,
                   const MATH::Vec3& toTangent, float weight, float span) noexcept {
    const float t2 = weight * weight;
    const float t3 = t2 * weight;
    const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    const float h10 = t3 - 2.0f * t2 + weight;
    const float h01 = -2.0f * t3 + 3.0f * t2;
    const float h11 = t3 - t2;
    return from * h00 + fromTangent * (h10 * span) + to * h01 + toTangent * (h11 * span);
}

MATH::Quat Hermite(const MATH::Quat& from, const MATH::Quat& fromTangent, const MATH::Quat& to,
                   const MATH::Quat& toTangent, float weight, float span) noexcept {
    const float t2 = weight * weight;
    const float t3 = t2 * weight;
    const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    const float h10 = t3 - 2.0f * t2 + weight;
    const float h01 = -2.0f * t3 + 3.0f * t2;
    const float h11 = t3 - t2;
    return MATH::NormalizeQ({from.x * h00 + fromTangent.x * (h10 * span) + to.x * h01 + toTangent.x * (h11 * span),
                             from.y * h00 + fromTangent.y * (h10 * span) + to.y * h01 + toTangent.y * (h11 * span),
                             from.z * h00 + fromTangent.z * (h10 * span) + to.z * h01 + toTangent.z * (h11 * span),
                             from.w * h00 + fromTangent.w * (h10 * span) + to.w * h01 + toTangent.w * (h11 * span)});
}

template <class Key>
size_t FindUpperKey(const std::vector<Key>& keys, float time) {
    return static_cast<size_t>(std::lower_bound(keys.begin(), keys.end(), time,
                                                [](const Key& key, float value) { return key.timeSec < value; }) -
                               keys.begin());
}

MATH::Vec3 SampleVec3(const std::vector<AnimationKeyframe<MATH::Vec3>>& keys, float time,
                      AnimationInterpolation interpolation, const MATH::Vec3& fallback) {
    if (keys.empty())
        return fallback;
    if (keys.size() == 1u || time <= keys.front().timeSec) {
        return keys.front().value;
    }
    if (time >= keys.back().timeSec)
        return keys.back().value;
    const size_t upper = FindUpperKey(keys, time);
    if (upper == 0u || upper >= keys.size())
        return keys.back().value;
    const auto& from = keys[upper - 1u];
    const auto& to = keys[upper];
    if (interpolation == AnimationInterpolation::Step) {
        return from.value;
    }
    const float span = (std::max)(to.timeSec - from.timeSec, 1.0e-5f);
    const float weight = ClampUnit((time - from.timeSec) / span);
    return interpolation == AnimationInterpolation::CubicSpline
               ? Hermite(from.value, from.outTangent, to.value, to.inTangent, weight, span)
               : Lerp(from.value, to.value, weight);
}

MATH::Quat SampleQuat(const std::vector<AnimationKeyframe<MATH::Quat>>& keys, float time,
                      AnimationInterpolation interpolation, const MATH::Quat& fallback) {
    if (keys.empty())
        return fallback;
    if (keys.size() == 1u || time <= keys.front().timeSec) {
        return MATH::NormalizeQ(keys.front().value);
    }
    if (time >= keys.back().timeSec) {
        return MATH::NormalizeQ(keys.back().value);
    }
    const size_t upper = FindUpperKey(keys, time);
    if (upper == 0u || upper >= keys.size()) {
        return MATH::NormalizeQ(keys.back().value);
    }
    const auto& from = keys[upper - 1u];
    const auto& to = keys[upper];
    if (interpolation == AnimationInterpolation::Step) {
        return MATH::NormalizeQ(from.value);
    }
    const float span = (std::max)(to.timeSec - from.timeSec, 1.0e-5f);
    const float weight = ClampUnit((time - from.timeSec) / span);
    return interpolation == AnimationInterpolation::CubicSpline
               ? Hermite(from.value, from.outTangent, to.value, to.inTangent, weight, span)
               : MATH::NlerpQ(from.value, to.value, weight);
}
} // namespace

const AnimationClip* ResolveAnimationClip(const ModelAsset& model, const AnimationClipReference& reference) noexcept {
    if (!reference.modelAssetId.value.empty() && !model.id.value.empty() && reference.modelAssetId != model.id) {
        return nullptr;
    }
    if (reference.clipId.IsValid()) {
        if (const AnimationClip* clip = model.FindAnimationClip(reference.clipId)) {
            return clip;
        }
    }
    return !reference.fallbackName.empty() ? model.FindAnimationClip(reference.fallbackName) : nullptr;
}

AnimationClipReference MakeAnimationClipReference(const ModelAsset& model, size_t clipIndex) {
    AnimationClipReference result{};
    const AnimationClip* clip = model.GetAnimationClip(clipIndex);
    if (clip == nullptr)
        return result;
    result.modelAssetId = model.id;
    result.clipId = model.GetAnimationClipId(clipIndex);
    result.fallbackName = clip->name;
    return result;
}

void BuildBindPose(const ModelAsset& model, AnimationLocalPose& outPose) {
    outPose.nodes.resize(model.nodes.size());
    for (size_t index = 0u; index < model.nodes.size(); ++index) {
        outPose.nodes[index] = model.nodes[index].localTransform;
    }
}

bool SampleAnimationClip(const ModelAsset& model, const AnimationClipReference& reference, float timeSeconds, bool loop,
                         AnimationLocalPose& outPose, float* outNormalizedTime) {
    const AnimationClip* clip = ResolveAnimationClip(model, reference);
    if (clip == nullptr) {
        BuildBindPose(model, outPose);
        if (outNormalizedTime != nullptr)
            *outNormalizedTime = 0.0f;
        return false;
    }

    return SampleResolvedAnimationClip(model, *clip, timeSeconds, loop, outPose, outNormalizedTime);
}

bool SampleResolvedAnimationClip(const ModelAsset& model, const AnimationClip& clip, float timeSeconds, bool loop,
                                 AnimationLocalPose& outPose, float* outNormalizedTime) {
    BuildBindPose(model, outPose);

    const float sampleTime = ResolveSampleTime(timeSeconds, loop, clip.durationSec);
    if (outNormalizedTime != nullptr) {
        *outNormalizedTime = clip.durationSec > 1.0e-5f ? ClampUnit(sampleTime / clip.durationSec) : 0.0f;
    }
    for (const NodeAnimationChannel& channel : clip.channels) {
        if (channel.targetNode < 0 || channel.targetNode >= static_cast<int>(outPose.nodes.size())) {
            continue;
        }
        Transform3D& node = outPose.nodes[static_cast<size_t>(channel.targetNode)];
        switch (channel.path) {
        case AnimationTargetPath::Translation:
            node.position = SampleVec3(channel.vec3Keys, sampleTime, channel.interpolation, node.position);
            break;
        case AnimationTargetPath::Rotation:
            node.rotation = SampleQuat(channel.quatKeys, sampleTime, channel.interpolation, node.rotation);
            break;
        case AnimationTargetPath::Scale:
            node.scale = SampleVec3(channel.vec3Keys, sampleTime, channel.interpolation, node.scale);
            break;
        default: break;
        }
    }
    return true;
}

bool BlendAnimationPoses(const AnimationLocalPose& from, const AnimationLocalPose& to, float weight,
                         AnimationLocalPose& outPose) {
    if (from.nodes.size() != to.nodes.size() || to.nodes.empty()) {
        return false;
    }
    const float safeWeight = ClampUnit(weight);
    outPose.nodes.resize(to.nodes.size());
    for (size_t index = 0u; index < to.nodes.size(); ++index) {
        const Transform3D& a = from.nodes[index];
        const Transform3D& b = to.nodes[index];
        Transform3D result{};
        result.position = Lerp(a.position, b.position, safeWeight);
        result.rotation = MATH::NlerpQ(a.rotation, b.rotation, safeWeight);
        result.scale = Lerp(a.scale, b.scale, safeWeight);
        outPose.nodes[index] = result;
    }
    return true;
}

bool BlendAnimationPoseSubtree(const ModelAsset& model, const AnimationLocalPose& from, const AnimationLocalPose& to,
                               std::string_view maskRootNodeName, float weight, AnimationLocalPose& outPose,
                               AnimationSubtreeRootAlignment rootAlignment) {
    if (!std::isfinite(weight) || model.nodes.empty() || !from.IsValidFor(model.nodes.size()) ||
        !to.IsValidFor(model.nodes.size()) || maskRootNodeName.empty() ||
        (rootAlignment != AnimationSubtreeRootAlignment::ParentLocal &&
         rootAlignment != AnimationSubtreeRootAlignment::PreserveModelRotation)) {
        return false;
    }

    AnimationPoseSubtreeCache cache{};
    if (!PrepareAnimationPoseSubtreeCache(model, maskRootNodeName, {}, cache)) {
        return false;
    }
    return BlendAnimationPoseSubtreeCached(model, from, to, cache, weight, outPose, rootAlignment);
}

bool PrepareAnimationPoseSubtreeCache(const ModelAsset& model, std::string_view maskRootNodeName,
                                      const AnimationClipReference& clipReference,
                                      AnimationPoseSubtreeCache& outCache) {
    outCache.Reset();
    if (model.nodes.empty() || maskRootNodeName.empty()) {
        return false;
    }

    const AnimationClip* clip = nullptr;
    if (!clipReference.IsEmpty()) {
        clip = ResolveAnimationClip(model, clipReference);
        if (clip == nullptr) {
            return false;
        }
    }

    int rootNode = -1;
    if (!POSE_EVALUATION::ResolveNodeByExactName(model, maskRootNodeName, rootNode)) {
        return false;
    }

    std::vector<size_t> subtreeNodes{};
    subtreeNodes.reserve(model.nodes.size());
    const size_t nodeCount = model.nodes.size();
    for (size_t index = 0u; index < nodeCount; ++index) {
        int cursor = static_cast<int>(index);
        bool inSubtree = false;
        for (size_t step = 0u; step <= nodeCount; ++step) {
            if (cursor == rootNode) {
                inSubtree = true;
                break;
            }
            if (!POSE_EVALUATION::IsValidNodeIndex(model, cursor)) {
                break;
            }
            cursor = model.nodes[static_cast<size_t>(cursor)].parent;
        }
        if (inSubtree) {
            subtreeNodes.push_back(index);
        }
    }

    // Store the parent chain in evaluation order (highest ancestor first)
    // so root-alignment can compose only the chain it actually needs.
    std::vector<int> rootPath{};
    rootPath.reserve(nodeCount);
    int cursor = rootNode;
    bool reachedHierarchyRoot = false;
    for (size_t step = 0u; step <= nodeCount; ++step) {
        if (!POSE_EVALUATION::IsValidNodeIndex(model, cursor)) {
            return false;
        }
        rootPath.push_back(cursor);
        const int parent = model.nodes[static_cast<size_t>(cursor)].parent;
        if (parent < 0) {
            reachedHierarchyRoot = true;
            break;
        }
        cursor = parent;
    }
    if (!reachedHierarchyRoot || rootPath.empty() || rootPath.size() > nodeCount) {
        return false;
    }
    std::reverse(rootPath.begin(), rootPath.end());

    outCache.model = &model;
    outCache.modelSourceRevision = model.sourceRevision;
    outCache.clip = clip;
    outCache.maskRootNode = rootNode;
    outCache.rootMotionNode = POSE_EVALUATION::ResolveRootNode(model);
    outCache.subtreeNodes = std::move(subtreeNodes);
    outCache.rootPath = std::move(rootPath);
    outCache.overlayPose.nodes.reserve(model.nodes.size());
    outCache.valid = true;
    return true;
}

namespace {

bool IsValidRootAlignment(AnimationSubtreeRootAlignment rootAlignment) noexcept {
    return rootAlignment == AnimationSubtreeRootAlignment::ParentLocal ||
           rootAlignment == AnimationSubtreeRootAlignment::PreserveModelRotation;
}

bool ValidateCachedSubtree(const ModelAsset& model, const AnimationLocalPose& from, const AnimationLocalPose& to,
                           const AnimationPoseSubtreeCache& cache, bool validateValues) noexcept {
    if (!cache.valid || cache.model != &model || cache.modelSourceRevision != model.sourceRevision ||
        model.nodes.empty() || !from.IsValidFor(model.nodes.size()) || !to.IsValidFor(model.nodes.size()) ||
        !POSE_EVALUATION::IsValidNodeIndex(model, cache.maskRootNode) || cache.rootPath.empty()) {
        return false;
    }
    if (!validateValues) {
        return true;
    }
    for (const size_t index : cache.subtreeNodes) {
        if (index >= model.nodes.size() || static_cast<int>(index) == cache.rootMotionNode) {
            continue;
        }
        const Transform3D& fromNode = from.nodes[index];
        const Transform3D& toNode = to.nodes[index];
        if (!MATH::IsFinite(fromNode.position) || !MATH::IsFinite(fromNode.rotation) ||
            !MATH::IsFinite(fromNode.scale) || !MATH::IsFinite(toNode.position) || !MATH::IsFinite(toNode.rotation) ||
            !MATH::IsFinite(toNode.scale)) {
            return false;
        }
    }
    return true;
}

bool ResolveAlignedRootRotation(const ModelAsset& model, const AnimationLocalPose& from, const AnimationLocalPose& to,
                                const AnimationPoseSubtreeCache& cache, AnimationSubtreeRootAlignment rootAlignment,
                                MATH::Quat& outRotation) noexcept {
    outRotation = to.nodes[static_cast<size_t>(cache.maskRootNode)].rotation;
    if (rootAlignment == AnimationSubtreeRootAlignment::ParentLocal) {
        return MATH::IsFinite(outRotation);
    }

    MATH::Mat4 baseParentGlobal = MATH::Mat4::Identity();
    MATH::Mat4 overlayRootGlobal = MATH::Mat4::Identity();
    for (size_t pathIndex = 0u; pathIndex < cache.rootPath.size(); ++pathIndex) {
        const int nodeIndex = cache.rootPath[pathIndex];
        if (!POSE_EVALUATION::IsValidNodeIndex(model, nodeIndex)) {
            return false;
        }
        const size_t index = static_cast<size_t>(nodeIndex);
        overlayRootGlobal = overlayRootGlobal * POSE_EVALUATION::GetNodeLocalMatrix(model, index, &to);
        // The final element is the masked root.  Its parent global
        // must be computed from the base pose only.
        if (pathIndex + 1u < cache.rootPath.size()) {
            baseParentGlobal = baseParentGlobal * POSE_EVALUATION::GetNodeLocalMatrix(model, index, &from);
        }
    }

    const MATH::Mat4 alignedLocal = MATH::Inverse(baseParentGlobal) * overlayRootGlobal;
    MATH::Vec3 ignoredTranslation{};
    MATH::Vec3 ignoredScale{};
    if (!MATH::DecomposeTRS(alignedLocal, ignoredTranslation, outRotation, ignoredScale) ||
        !MATH::IsFinite(outRotation)) {
        return false;
    }
    return true;
}

void BlendCachedNodes(const AnimationLocalPose& from, const AnimationLocalPose& to,
                      const AnimationPoseSubtreeCache& cache, float safeWeight, const MATH::Quat& alignedRootRotation,
                      AnimationLocalPose& outPose) {
    outPose.nodes = from.nodes;
    for (const size_t index : cache.subtreeNodes) {
        if (index >= from.nodes.size() || static_cast<int>(index) == cache.rootMotionNode) {
            continue;
        }
        const Transform3D& fromNode = from.nodes[index];
        const Transform3D& toNode = to.nodes[index];
        const MATH::Quat& targetRotation =
            static_cast<int>(index) == cache.maskRootNode ? alignedRootRotation : toNode.rotation;
        Transform3D& result = outPose.nodes[index];
        result.position = Lerp(fromNode.position, toNode.position, safeWeight);
        result.rotation = MATH::NlerpQ(fromNode.rotation, targetRotation, safeWeight);
        result.scale = Lerp(fromNode.scale, toNode.scale, safeWeight);
        result.useExplicitMatrix = false;
        result.explicitMatrix = {};
    }
}

void ApplyCachedNodesInPlace(AnimationLocalPose& inOutPose, const AnimationLocalPose& to,
                             const AnimationPoseSubtreeCache& cache, float safeWeight,
                             const MATH::Quat& alignedRootRotation) {
    for (const size_t index : cache.subtreeNodes) {
        if (index >= inOutPose.nodes.size() || static_cast<int>(index) == cache.rootMotionNode) {
            continue;
        }
        const Transform3D& fromNode = inOutPose.nodes[index];
        const Transform3D& toNode = to.nodes[index];
        const MATH::Quat targetRotation =
            static_cast<int>(index) == cache.maskRootNode ? alignedRootRotation : toNode.rotation;
        Transform3D& result = inOutPose.nodes[index];
        result.position = Lerp(fromNode.position, toNode.position, safeWeight);
        result.rotation = MATH::NlerpQ(fromNode.rotation, targetRotation, safeWeight);
        result.scale = Lerp(fromNode.scale, toNode.scale, safeWeight);
        result.useExplicitMatrix = false;
        result.explicitMatrix = {};
    }
}

} // namespace

bool BlendAnimationPoseSubtreeCached(const ModelAsset& model, const AnimationLocalPose& from,
                                     const AnimationLocalPose& to, const AnimationPoseSubtreeCache& cache, float weight,
                                     AnimationLocalPose& outPose, AnimationSubtreeRootAlignment rootAlignment) {
    if (!std::isfinite(weight) || !IsValidRootAlignment(rootAlignment) ||
        !ValidateCachedSubtree(model, from, to, cache, false)) {
        return false;
    }

    const float safeWeight = ClampUnit(weight);
    if (safeWeight <= 0.0f) {
        outPose.nodes = from.nodes;
        return true;
    }
    if (!ValidateCachedSubtree(model, from, to, cache, true)) {
        return false;
    }

    MATH::Quat alignedRootRotation{};
    if (!ResolveAlignedRootRotation(model, from, to, cache, rootAlignment, alignedRootRotation)) {
        return false;
    }
    BlendCachedNodes(from, to, cache, safeWeight, alignedRootRotation, outPose);
    return true;
}

bool ApplyAnimationPoseSubtreeInPlace(const ModelAsset& model, AnimationLocalPose& inOutPose,
                                      const AnimationLocalPose& to, const AnimationPoseSubtreeCache& cache,
                                      float weight, AnimationSubtreeRootAlignment rootAlignment) {
    if (!std::isfinite(weight) || !IsValidRootAlignment(rootAlignment) ||
        !ValidateCachedSubtree(model, inOutPose, to, cache, false)) {
        return false;
    }

    const float safeWeight = ClampUnit(weight);
    if (safeWeight <= 0.0f) {
        return true;
    }
    if (!ValidateCachedSubtree(model, inOutPose, to, cache, true)) {
        return false;
    }
    MATH::Quat alignedRootRotation{};
    if (!ResolveAlignedRootRotation(model, inOutPose, to, cache, rootAlignment, alignedRootRotation)) {
        return false;
    }
    ApplyCachedNodesInPlace(inOutPose, to, cache, safeWeight, alignedRootRotation);
    return true;
}

} // namespace HIKARI::ANIMATION
