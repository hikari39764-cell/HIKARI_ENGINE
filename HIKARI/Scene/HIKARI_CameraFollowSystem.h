#pragma once

#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

    class Camera3D;
    class CameraComponent;
    class CameraFollowComponent;
    class GameObject;
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
            GameplayCameraSafetyResult* outResult = nullptr,
            const CameraFollowComponent* previewComponent = nullptr);
    }
    namespace GAMEPLAY { class MotionIntentService; }
    namespace INPUT { class InputService; class InputSnapshot; }
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

        bool EvaluatePreview(World& world, GameObject& camera, CameraFollowComponent& preview,
            float seconds, const INPUT::InputSnapshot* input = nullptr);
    private:
        void EvaluateCamera(GameObject& camera, CameraComponent& lens, CameraFollowComponent& component,
            const INPUT::InputSnapshot* input, float dt, float cameraDt, uint64_t frameIndex);
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
