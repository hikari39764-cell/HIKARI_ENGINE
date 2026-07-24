#pragma once

#include <memory>
#include <unordered_map>

#include "Animation/Runtime/HIKARI_AnimationPose.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI::ANIMATION {

    class AnimationPoseService {
    public:
        bool Publish(
            RuntimeObjectHandle object,
            AnimationPoseSnapshot snapshot);
        std::shared_ptr<const AnimationPoseSnapshot> Find(
            RuntimeObjectHandle object) const noexcept;
        void Remove(RuntimeObjectHandle object) noexcept;
        void Clear() noexcept;

    private:
        std::unordered_map<
            uint64_t,
            std::shared_ptr<AnimationPoseSnapshot>> poses_{};
    };

} // namespace HIKARI::ANIMATION
