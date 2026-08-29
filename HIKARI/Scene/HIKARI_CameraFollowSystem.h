#pragma once

#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

    class Camera3D;
    struct RuntimePlayStateService;
    struct GameplayCameraService;
    class PresentationTransformService;
    namespace CAMERA {
        class CameraRigService;

        struct GameplayCameraSafetyResult {
            bool evaluated = false;
            bool collisionLimited = false;
            bool primaryVisible = false;
            bool usedFollowFallback = false;
            bool emergencyContraction = false;
            float desiredDistance = 0.0f;
            float resolvedDistance = 0.0f;
        };

        bool ResolveGameplayCameraSafety(
            const World& world,
            SceneObjectId cameraObjectId,
            Camera3D& camera,
            GameplayCameraSafetyResult* outResult = nullptr);
    }
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
        const GameplayCameraService* gameplayCameraService_ = nullptr;
    };

} // namespace HIKARI
