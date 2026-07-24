#pragma once

#include <vector>

#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI {
    class AnimationStateMachineAssetStore;
    namespace ANIMATION { class AnimationStateMachineRuntimeService; }
    namespace GAMEPLAY { class CharacterMotionStateService; }

    class AnimationStateMachineSystem final : public ISystem {
    public:
        std::string_view GetName() const override {
            return "AnimationStateMachineSystem";
        }

        void OnWorldAttached(World& world) override;
        void OnWorldDetached(World& world) override;
        void Update(World& world, const FrameContext& frame) override;

    private:
        AnimationStateMachineAssetStore* assetStore_ = nullptr;
        ANIMATION::AnimationStateMachineRuntimeService* runtimeService_ =
            nullptr;
        GAMEPLAY::CharacterMotionStateService* motionStateService_ = nullptr;
        std::vector<RuntimeObjectHandle> activeObjects_{};
    };

} // namespace HIKARI
