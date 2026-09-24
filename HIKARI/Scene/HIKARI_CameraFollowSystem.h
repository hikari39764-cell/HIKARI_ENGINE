#pragma once

#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {

class CameraComponent;
class CameraFollowComponent;
class GameObject;
struct RuntimePlayStateService;
struct GameplayCameraService;
class PresentationTransformService;
namespace CAMERA {
class CameraRigService;
}
namespace GAMEPLAY {
class MotionIntentService;
}
namespace INPUT {
class InputService;
class InputSnapshot;
} // namespace INPUT
namespace PHYSICS {
class PhysicsWorldService;
}

class CameraFollowSystem final : public ISystem {
public:
    std::string_view GetName() const override { return "CameraFollowSystem"; }
    void OnWorldAttached(World& world) override;
    void OnWorldDetached(World& world) override;
    void LateUpdate(World& world, const FrameContext& frame) override;

    bool EvaluatePreview(World& world, GameObject& camera, CameraFollowComponent& preview, float seconds,
                         const INPUT::InputSnapshot* input = nullptr);

private:
    void EvaluateCamera(GameObject& camera, CameraComponent& lens, CameraFollowComponent& component,
                        const INPUT::InputSnapshot* input, float dt, float cameraDt, uint64_t frameIndex);
    CAMERA::CameraRigService* rigService_ = nullptr;
    GAMEPLAY::MotionIntentService* motionIntentService_ = nullptr;
    INPUT::InputService* inputService_ = nullptr;
    PHYSICS::PhysicsWorldService* physicsService_ = nullptr;
    const PresentationTransformService* presentationTransforms_ = nullptr;
    const RuntimePlayStateService* runtimePlayState_ = nullptr;
    const GameplayCameraService* gameplayCameraService_ = nullptr;
};

} // namespace HIKARI
