#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {

    struct AnimationClipId {
        uint64_t value = 0u;

        bool IsValid() const noexcept { return value != 0u; }

        friend bool operator==(
            const AnimationClipId&,
            const AnimationClipId&) = default;
    };

    inline AnimationClipId MakeAnimationClipId(
        std::string_view clipName,
        size_t ordinal) noexcept {

        uint64_t hash = HASH::Fnv1aBytes(
            HASH::kFnv1a64OffsetBasis,
            clipName.data(),
            clipName.size());
        const unsigned char separator = 0xffu;
        hash = HASH::Fnv1aBytes(
            hash, &separator, sizeof(separator));
        for (size_t shift = 0u; shift < sizeof(size_t) * 8u; shift += 8u) {
            const unsigned char byte = static_cast<unsigned char>(
                (ordinal >> shift) & 0xffu);
            hash = HASH::Fnv1aBytes(
                hash, &byte, sizeof(byte));
        }
        return { hash != 0u ? hash : 1u };
    }

    struct SkeletonJoint {
        std::string name{};
        int nodeIndex = -1;
        int parentJoint = -1;
        MATH::Mat4 inverseBindMatrix{};
    };

    struct SkeletonAsset {
        std::string name{};
        int skeletonRootNode = -1;
        std::vector<SkeletonJoint> joints{};
    };

    // HMODEL v1-v3 wrote these enums directly. Keep their original storage
    // size so the versioned reader can migrate existing imported assets.
    enum class AnimationTargetPath {
        Translation,
        Rotation,
        Scale,
        Weights,
    };

    enum class AnimationInterpolation {
        Step,
        Linear,
        CubicSpline,
    };

    template<class T>
    struct AnimationKeyframe {
        float timeSec = 0.0f;
        T value{};
        T inTangent{};
        T outTangent{};
    };

    struct NodeAnimationChannel {
        int targetNode = -1;
        AnimationTargetPath path = AnimationTargetPath::Translation;
        AnimationInterpolation interpolation =
            AnimationInterpolation::Linear;
        std::vector<AnimationKeyframe<MATH::Vec3>> vec3Keys{};
        std::vector<AnimationKeyframe<MATH::Quat>> quatKeys{};
    };

    struct AnimationClip {
        std::string name{};
        float durationSec = 0.0f;
        std::vector<NodeAnimationChannel> channels{};
    };

} // namespace HIKARI
