#pragma once

#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {

    struct RuntimePlayStateService;
    class PresentationTransformService;
    namespace CAMERA { class CameraRigService; }
    namespace GAMEPLAY { class MotionIntentService; }
    namespace INPUT { class InputService; }
    namespace PHYSICS { class PhysicsWorldService; }

    class CameraFollowSystem final : public ISystem {
    public:
        std::string_view GetName() const override {
            return "CameraFollowSystem";
        }
        void OnWorldAttached(World& world) override;
        void OnWorldDetached(World& world) override;
        void LateUpdate(
            World& world,
            const FrameContext& frame) override;

    private:
        CAMERA::CameraRigService* rigService_ = nullptr;
        GAMEPLAY::MotionIntentService* motionIntentService_ = nullptr;
        INPUT::InputService* inputService_ = nullptr;
        PHYSICS::PhysicsWorldService* physicsService_ = nullptr;
        const PresentationTransformService*
            presentationTransforms_ = nullptr;
        const RuntimePlayStateService* runtimePlayState_ = nullptr;
    };

} // namespace HIKARI
