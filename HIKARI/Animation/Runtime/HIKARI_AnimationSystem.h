#pragma once

#include <vector>

#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI {
    namespace ANIMATION { class AnimationPoseService; }

    class AnimationSystem final : public ISystem {
    public:
        std::string_view GetName() const override {
            return "AnimationSystem";
        }

        void OnWorldAttached(World& world) override;
        void OnWorldDetached(World& world) override;
        void Update(World& world, const FrameContext& frame) override;

    private:
        ANIMATION::AnimationPoseService* poseService_ = nullptr;
        std::vector<RuntimeObjectHandle> publishedObjects_{};
    };

} // namespace HIKARI
