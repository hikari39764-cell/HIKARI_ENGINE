#pragma once

#include "Core/Math/HIKARI_Math3D.h"
#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {
class World;
class GameObject;
class Camera3D;
class CameraFollowComponent;
namespace PHYSICS {
class PhysicsWorldService;
}
namespace CAMERA {
struct GameplayCameraSafetyResult {
    bool evaluated = false;
    bool collisionLimited = false;
    bool primaryVisible = false;
    bool usedFollowFallback = false;
    bool emergencyContraction = false;
    float desiredDistance = 0.0f;
    float resolvedDistance = 0.0f;
};

bool ResolveGameplayCameraSafety(const World& world, SceneObjectId cameraObjectId, Camera3D& camera,
                                 GameplayCameraSafetyResult* outResult = nullptr,
                                 const CameraFollowComponent* previewComponent = nullptr);
namespace FOLLOW {
float ResolveCollisionDistance(const PHYSICS::PhysicsWorldService* physics, const CameraFollowComponent& component,
                               const GameObject& target, const MATH::Vec3& pivot, const MATH::Vec3& direction,
                               float desiredDistance, bool& outLimited, float queryRadius = 0.0f);
}
} // namespace CAMERA
} // namespace HIKARI
