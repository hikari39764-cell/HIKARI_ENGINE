#pragma once

#include <cstdint>
#include <unordered_map>

#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {
    struct GameplayCameraService;
    namespace GAMEPLAY { class MotionIntentService; }
    namespace PHYSICS {
        class KinematicMotionService;
        class PhysicsWorldService;
    }

    class CharacterLocomotionSystem final : public ISystem {
    public:
        std::string_view GetName() const override {
            return "CharacterLocomotionSystem";
        }

        void OnWorldAttached(World& world) override;
        void OnWorldDetached(World& world) override;
        void FixedUpdate(
            World& world,
            const FrameContext& frame) override;

    private:
        struct RuntimeState {
            float jumpBufferRemaining = 0.0f;
            float groundGraceRemaining = 0.0f;
            uint64_t lastTouchedFixedTick = 0u;
            bool jumpHeldLastFixedTick = false;
        };

        GAMEPLAY::MotionIntentService* motionIntentService_ = nullptr;
        PHYSICS::KinematicMotionService* kinematicMotionService_ = nullptr;
        const PHYSICS::PhysicsWorldService* physicsService_ = nullptr;
        const GameplayCameraService* cameraService_ = nullptr;
        std::unordered_map<uint64_t, RuntimeState> runtimeStates_{};
    };

} // namespace HIKARI
