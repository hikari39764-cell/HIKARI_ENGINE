#include "Animation/Runtime/HIKARI_AnimationClipSampler.h"

#include <algorithm>
#include <cmath>

#include "Assets/Models/HIKARI_ModelAsset.h"

namespace HIKARI::ANIMATION {
    namespace {
        float ClampUnit(float value) noexcept {
            return std::clamp(
                std::isfinite(value) ? value : 0.0f,
                0.0f,
                1.0f);
        }

        float ResolveSampleTime(
            float timeSeconds,
            bool loop,
            float durationSeconds) noexcept {
            float result = std::isfinite(timeSeconds)
                ? timeSeconds
                : 0.0f;
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

        MATH::Vec3 Lerp(
            const MATH::Vec3& from,
            const MATH::Vec3& to,
            float weight) noexcept {
            return from + (to - from) * weight;
        }

        MATH::Quat Nlerp(
            const MATH::Quat& from,
            const MATH::Quat& to,
            float weight) noexcept {
            MATH::Quat end = to;
            const float dot = from.x * to.x + from.y * to.y +
                from.z * to.z + from.w * to.w;
            if (dot < 0.0f) {
                end = { -to.x, -to.y, -to.z, -to.w };
            }
            return MATH::NormalizeQ({
                from.x + (end.x - from.x) * weight,
                from.y + (end.y - from.y) * weight,
                from.z + (end.z - from.z) * weight,
                from.w + (end.w - from.w) * weight
            });
        }

        MATH::Vec3 Hermite(
            const MATH::Vec3& from,
            const MATH::Vec3& fromTangent,
            const MATH::Vec3& to,
            const MATH::Vec3& toTangent,
            float weight,
            float span) noexcept {
            const float t2 = weight * weight;
            const float t3 = t2 * weight;
            const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
            const float h10 = t3 - 2.0f * t2 + weight;
            const float h01 = -2.0f * t3 + 3.0f * t2;
            const float h11 = t3 - t2;
            return from * h00 + fromTangent * (h10 * span) +
                to * h01 + toTangent * (h11 * span);
        }

        MATH::Quat Hermite(
            const MATH::Quat& from,
            const MATH::Quat& fromTangent,
            const MATH::Quat& to,
            const MATH::Quat& toTangent,
            float weight,
            float span) noexcept {
            const float t2 = weight * weight;
            const float t3 = t2 * weight;
            const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
            const float h10 = t3 - 2.0f * t2 + weight;
            const float h01 = -2.0f * t3 + 3.0f * t2;
            const float h11 = t3 - t2;
            return MATH::NormalizeQ({
                from.x * h00 + fromTangent.x * (h10 * span) +
                    to.x * h01 + toTangent.x * (h11 * span),
                from.y * h00 + fromTangent.y * (h10 * span) +
                    to.y * h01 + toTangent.y * (h11 * span),
                from.z * h00 + fromTangent.z * (h10 * span) +
                    to.z * h01 + toTangent.z * (h11 * span),
                from.w * h00 + fromTangent.w * (h10 * span) +
                    to.w * h01 + toTangent.w * (h11 * span)
            });
        }

        template<class Key>
        size_t FindUpperKey(const std::vector<Key>& keys, float time) {
            return static_cast<size_t>(std::lower_bound(
                keys.begin(),
                keys.end(),
                time,
                [](const Key& key, float value) {
                    return key.timeSec < value;
                }) - keys.begin());
        }

        MATH::Vec3 SampleVec3(
            const std::vector<AnimationKeyframe<MATH::Vec3>>& keys,
            float time,
            AnimationInterpolation interpolation,
            const MATH::Vec3& fallback) {
            if (keys.empty()) return fallback;
            if (keys.size() == 1u || time <= keys.front().timeSec) {
                return keys.front().value;
            }
            if (time >= keys.back().timeSec) return keys.back().value;
            const size_t upper = FindUpperKey(keys, time);
            if (upper == 0u || upper >= keys.size()) return keys.back().value;
            const auto& from = keys[upper - 1u];
            const auto& to = keys[upper];
            if (interpolation == AnimationInterpolation::Step) {
                return from.value;
            }
            const float span = (std::max)(to.timeSec - from.timeSec, 1.0e-5f);
            const float weight = ClampUnit((time - from.timeSec) / span);
            return interpolation == AnimationInterpolation::CubicSpline
                ? Hermite(
                    from.value,
                    from.outTangent,
                    to.value,
                    to.inTangent,
                    weight,
                    span)
                : Lerp(from.value, to.value, weight);
        }

        MATH::Quat SampleQuat(
            const std::vector<AnimationKeyframe<MATH::Quat>>& keys,
            float time,
            AnimationInterpolation interpolation,
            const MATH::Quat& fallback) {
            if (keys.empty()) return fallback;
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
                ? Hermite(
                    from.value,
                    from.outTangent,
                    to.value,
                    to.inTangent,
                    weight,
                    span)
                : Nlerp(from.value, to.value, weight);
        }
    }

    const AnimationClip* ResolveAnimationClip(
        const ModelAsset& model,
        const AnimationClipReference& reference) noexcept {
        if (!reference.modelAssetId.value.empty() &&
            !model.id.value.empty() &&
            reference.modelAssetId != model.id) {
            return nullptr;
        }
        if (reference.clipId.IsValid()) {
            if (const AnimationClip* clip =
                    model.FindAnimationClip(reference.clipId)) {
                return clip;
            }
        }
        return !reference.fallbackName.empty()
            ? model.FindAnimationClip(reference.fallbackName)
            : nullptr;
    }

    AnimationClipReference MakeAnimationClipReference(
        const ModelAsset& model,
        size_t clipIndex) {
        AnimationClipReference result{};
        const AnimationClip* clip = model.GetAnimationClip(clipIndex);
        if (clip == nullptr) return result;
        result.modelAssetId = model.id;
        result.clipId = model.GetAnimationClipId(clipIndex);
        result.fallbackName = clip->name;
        return result;
    }

    void BuildBindPose(
        const ModelAsset& model,
        AnimationLocalPose& outPose) {
        outPose.nodes.resize(model.nodes.size());
        for (size_t index = 0u; index < model.nodes.size(); ++index) {
            outPose.nodes[index] = model.nodes[index].localTransform;
        }
    }

    bool SampleAnimationClip(
        const ModelAsset& model,
        const AnimationClipReference& reference,
        float timeSeconds,
        bool loop,
        AnimationLocalPose& outPose,
        float* outNormalizedTime) {
        BuildBindPose(model, outPose);
        const AnimationClip* clip = ResolveAnimationClip(model, reference);
        if (clip == nullptr) {
            if (outNormalizedTime != nullptr) *outNormalizedTime = 0.0f;
            return false;
        }

        const float sampleTime = ResolveSampleTime(
            timeSeconds,
            loop,
            clip->durationSec);
        if (outNormalizedTime != nullptr) {
            *outNormalizedTime = clip->durationSec > 1.0e-5f
                ? ClampUnit(sampleTime / clip->durationSec)
                : 0.0f;
        }
        for (const NodeAnimationChannel& channel : clip->channels) {
            if (channel.targetNode < 0 ||
                channel.targetNode >=
                    static_cast<int>(outPose.nodes.size())) {
                continue;
            }
            Transform3D& node = outPose.nodes[
                static_cast<size_t>(channel.targetNode)];
            switch (channel.path) {
            case AnimationTargetPath::Translation:
                node.position = SampleVec3(
                    channel.vec3Keys,
                    sampleTime,
                    channel.interpolation,
                    node.position);
                break;
            case AnimationTargetPath::Rotation:
                node.rotation = SampleQuat(
                    channel.quatKeys,
                    sampleTime,
                    channel.interpolation,
                    node.rotation);
                break;
            case AnimationTargetPath::Scale:
                node.scale = SampleVec3(
                    channel.vec3Keys,
                    sampleTime,
                    channel.interpolation,
                    node.scale);
                break;
            default:
                break;
            }
        }
        return true;
    }

    bool BlendAnimationPoses(
        const AnimationLocalPose& from,
        const AnimationLocalPose& to,
        float weight,
        AnimationLocalPose& outPose) {
        if (from.nodes.size() != to.nodes.size() || to.nodes.empty()) {
            return false;
        }
        const float safeWeight = ClampUnit(weight);
        outPose.nodes.resize(to.nodes.size());
        for (size_t index = 0u; index < to.nodes.size(); ++index) {
            const Transform3D& a = from.nodes[index];
            const Transform3D& b = to.nodes[index];
            Transform3D& result = outPose.nodes[index];
            result.position = Lerp(a.position, b.position, safeWeight);
            result.rotation = Nlerp(a.rotation, b.rotation, safeWeight);
            result.scale = Lerp(a.scale, b.scale, safeWeight);
        }
        return true;
    }

} // namespace HIKARI::ANIMATION
