#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Animation/Assets/HIKARI_AnimationAssetTypes.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/HIKARI_Transform3D.h"

namespace HIKARI::ANIMATION {

    struct AnimationClipReference {
        AssetId modelAssetId{};
        AnimationClipId clipId{};
        std::string fallbackName{};

        bool IsEmpty() const noexcept {
            return !clipId.IsValid() && fallbackName.empty();
        }

        friend bool operator==(
            const AnimationClipReference&,
            const AnimationClipReference&) = default;
    };

    struct AnimationLocalPose {
        std::vector<Transform3D> nodes{};

        bool IsValidFor(size_t nodeCount) const noexcept {
            return !nodes.empty() && nodes.size() == nodeCount;
        }
    };

    // A resolved runtime motion uses at most two synchronized clips. Higher
    // level authoring data (for example a 1D blend tree) is reduced to this
    // small playback contract before it reaches AnimatorComponent.
    struct AnimationMotionSample {
        AnimationClipReference primaryClip{};
        AnimationClipReference secondaryClip{};
        float secondaryWeight = 0.0f;

        bool IsEmpty() const noexcept {
            return primaryClip.IsEmpty();
        }

        bool IsBlended() const noexcept {
            return !secondaryClip.IsEmpty() && secondaryWeight > 0.0f;
        }
    };

    struct AnimationRootMotionDelta {
        MATH::Vec3 translation{};
        MATH::Quat rotation = MATH::Quat::Identity();
        bool valid = false;
    };

    struct AnimationPoseSnapshot {
        AnimationLocalPose localPose{};
        AnimationClipReference primaryClip{};
        AnimationClipReference secondaryClip{};
        float normalizedTime = 0.0f;
        float motionBlendWeight = 0.0f;
        float blendWeight = 1.0f;
        AnimationRootMotionDelta rootMotion{};
        uint64_t revision = 0u;
        bool transitioning = false;
        bool valid = false;
    };

} // namespace HIKARI::ANIMATION
