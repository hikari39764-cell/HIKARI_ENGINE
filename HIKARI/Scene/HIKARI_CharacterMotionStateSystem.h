#pragma once

#include <vector>

#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI {
    namespace GAMEPLAY {
        class CharacterMotionStateService;
        class MotionIntentService;
    }
    namespace PHYSICS { class KinematicMotionService; }

    class CharacterMotionStateSystem final : public ISystem {
    public:
        std::string_view GetName() const override {
            return "CharacterMotionStateSystem";
        }

        void OnWorldAttached(World& world) override;
        void OnWorldDetached(World& world) override;
        void PostFixedUpdate(
            World& world,
            const FrameContext& frame) override;

    private:
        GAMEPLAY::CharacterMotionStateService* stateService_ = nullptr;
        const GAMEPLAY::MotionIntentService* motionIntentService_ = nullptr;
        const PHYSICS::KinematicMotionService* kinematicMotionService_ =
            nullptr;
        std::vector<RuntimeObjectHandle> publishedObjects_{};
    };

} // namespace HIKARI
