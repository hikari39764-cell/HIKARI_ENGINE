#include "HIKARI_CameraCollision.h"

#include <algorithm>
#include <array>
#include <cmath>
#include "Core/Math/HIKARI_MathValidation.h"
#include "Physics/HIKARI_PhysicsWorldService.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Scene/Camera/HIKARI_CameraFollowMath.h"
#include "Scene/Components/HIKARI_CameraFollowComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {
namespace CAMERA::FOLLOW {
constexpr float kViewEpsilon = 1.0e-5f;
constexpr float kFinalCameraEmergencyDistance = 0.05f;
constexpr float kFinalCameraEmergencyFovDegrees = 10.0f;
constexpr float kPrimaryVisibilityVerticalOffset = 0.52f;
float ResolveCollisionDistance(const PHYSICS::PhysicsWorldService* physics, const CameraFollowComponent& component,
                               const GameObject& target, const MATH::Vec3& pivot, const MATH::Vec3& direction,
                               float desiredDistance, bool& outLimited, float queryRadius) {
    outLimited = false;
    if (!physics || !component.Settings().collisionEnabled || desiredDistance <= kViewEpsilon)
        return desiredDistance;
    PHYSICS::PhysicsShapeCastQuery query;
    query.shape.type = PHYSICS::PhysicsShapeType::Sphere;
    query.shape.radius = (std::max)(component.Settings().collisionRadius, queryRadius);
    query.startPose.position = pivot;
    query.direction = direction;
    query.maxDistance = desiredDistance;
    query.filter.layerMask = component.Settings().collisionLayerMask;
    query.filter.includeTriggers = false;
    query.filter.staticOnly = component.Settings().collisionStaticOnly;
    query.filter.ignoredObject = target.GetRuntimeHandle();
    PHYSICS::PhysicsHit hit;
    if (!physics->ShapeCast(query, hit))
        return desiredDistance;
    const float distance =
        std::clamp(hit.distance - component.Settings().collisionPadding, 0.001f, (std::max)(desiredDistance, 0.001f));
    outLimited = distance + kViewEpsilon < desiredDistance;
    return distance;
}

bool IsCameraPointVisible(const PHYSICS::PhysicsWorldService* physics, const CameraFollowComponent& component,
                          RuntimeObjectHandle targetHandle, const MATH::Vec3& eye, const MATH::Vec3& point) {
    if (physics == nullptr) {
        return true;
    }
    MATH::Vec3 delta = point - eye;
    const float distance = MATH::Length(delta);
    if (!MATH::IsFinite(delta) || !std::isfinite(distance) || distance <= kViewEpsilon) {
        return true;
    }
    delta = delta * (1.0f / distance);

    PHYSICS::PhysicsRaycastQuery query{};
    query.origin = eye;
    query.direction = delta;
    query.maxDistance = distance;
    query.filter.layerMask = component.Settings().collisionLayerMask;
    query.filter.includeTriggers = false;
    query.filter.staticOnly = component.Settings().collisionStaticOnly;
    query.filter.ignoredObject = targetHandle;
    PHYSICS::PhysicsHit hit{};
    return !physics->Raycast(query, hit) || hit.object == targetHandle;
}

bool HasPrimarySubjectVisibility(const PHYSICS::PhysicsWorldService* physics, const CameraFollowComponent& component,
                                 RuntimeObjectHandle targetHandle, const MATH::Vec3& eye,
                                 const MATH::Vec3& primaryPivot) {
    const std::array<MATH::Vec3, 3u> samples{
        primaryPivot,
        primaryPivot + MATH::Vec3{0.0f, kPrimaryVisibilityVerticalOffset, 0.0f},
        primaryPivot - MATH::Vec3{0.0f, kPrimaryVisibilityVerticalOffset, 0.0f},
    };
    return std::any_of(samples.begin(), samples.end(), [&](const MATH::Vec3& sample) {
        return IsCameraPointVisible(physics, component, targetHandle, eye, sample);
    });
}
} // namespace CAMERA::FOLLOW

namespace CAMERA {
using namespace FOLLOW;

bool ResolveGameplayCameraSafety(const World& world, SceneObjectId cameraObjectId, Camera3D& camera,
                                 GameplayCameraSafetyResult* outResult, const CameraFollowComponent* previewComponent) {
    GameplayCameraSafetyResult result{};
    const auto finish = [&]() {
        if (outResult != nullptr) {
            *outResult = result;
        }
    };

    const GameObject* cameraObject = world.FindObject(cameraObjectId);
    const CameraFollowComponent* component = previewComponent ? previewComponent
                                             : cameraObject != nullptr
                                                 ? cameraObject->GetComponent<CameraFollowComponent>()
                                                 : nullptr;
    if (component == nullptr || !component->IsEnabled() || !component->Runtime().initialized ||
        !component->Settings().collisionEnabled) {
        finish();
        return false;
    }

    const GameObject* target = world.FindObject(component->GetTargetObjectId());
    if (target == nullptr && cameraObject != nullptr && component->GetUseOwnerAsFallbackTarget()) {
        target = cameraObject->GetParent();
    }
    const PHYSICS::PhysicsWorldService* physics = world.Services().Find<PHYSICS::PhysicsWorldService>();
    if (target == nullptr || physics == nullptr) {
        finish();
        return false;
    }

    const MATH::Vec3 pivot = component->Runtime().followPivot;
    const MATH::Vec3 primaryPivot = component->Runtime().targetPivot;
    MATH::Vec3 safeEye = camera.GetPosition();
    MATH::Vec3 safeTarget = camera.GetTarget();
    MATH::Vec3 desiredOffset = safeEye - pivot;
    float desiredDistance = MATH::Length(desiredOffset);
    if (!MATH::IsFinite(pivot) || !MATH::IsFinite(primaryPivot) || !MATH::IsFinite(desiredOffset) ||
        !std::isfinite(desiredDistance) || desiredDistance <= kViewEpsilon) {
        finish();
        return false;
    }

    result.evaluated = true;
    result.desiredDistance = desiredDistance;
    const MATH::Vec3 desiredDirection = desiredOffset * (1.0f / desiredDistance);
    const float halfHeight = camera.GetNearZ() * std::tan(camera.GetFovYRad() * 0.5f);
    const float nearRadius = std::sqrt(camera.GetNearZ() * camera.GetNearZ() +
                                       halfHeight * halfHeight * (1 + camera.GetAspect() * camera.GetAspect()));
    bool collisionLimited = false;
    float safeDistance = ResolveCollisionDistance(physics, *component, *target, pivot, desiredDirection,
                                                  desiredDistance, collisionLimited, nearRadius);
    safeEye = pivot + desiredDirection * safeDistance;
    bool primaryVisible =
        HasPrimarySubjectVisibility(physics, *component, target->GetRuntimeHandle(), safeEye, primaryPivot);

    const bool firstPerson =
        component->GameplayState().source && component->GameplayState().mode.mount == CameraMount::FirstPerson;
    if (!primaryVisible && !firstPerson) {
        const MATH::Vec3 fallbackOffset = component->Runtime().eye - pivot;
        const float fallbackDistance = MATH::Length(fallbackOffset);
        if (MATH::IsFinite(fallbackOffset) && std::isfinite(fallbackDistance) && fallbackDistance > kViewEpsilon) {
            const MATH::Vec3 fallbackDirection = fallbackOffset * (1.0f / fallbackDistance);
            bool fallbackLimited = false;
            const float fallbackSafeDistance = ResolveCollisionDistance(
                physics, *component, *target, pivot, fallbackDirection, fallbackDistance, fallbackLimited, nearRadius);
            const MATH::Vec3 fallbackEye = pivot + fallbackDirection * fallbackSafeDistance;
            if (HasPrimarySubjectVisibility(physics, *component, target->GetRuntimeHandle(), fallbackEye,
                                            primaryPivot)) {
                safeEye = fallbackEye;
                safeTarget = component->Runtime().lookAt;
                safeDistance = fallbackSafeDistance;
                collisionLimited = collisionLimited || fallbackLimited;
                primaryVisible = true;
                result.usedFollowFallback = true;
            }
        }
    }

    result.collisionLimited = collisionLimited;
    result.primaryVisible = primaryVisible;
    result.resolvedDistance = MATH::Length(safeEye - pivot);
    const float authoredMinimum = (std::max)(component->Settings().minimumDistance, kFinalCameraEmergencyDistance);
    result.emergencyContraction = collisionLimited && result.resolvedDistance + kViewEpsilon < authoredMinimum;

    float verticalFov = camera.GetFovYRad();
    if (result.emergencyContraction) {
        const float pressure = std::clamp(1.0f - result.resolvedDistance / authoredMinimum, 0.0f, 1.0f);
        verticalFov += DegreesToRadians(kFinalCameraEmergencyFovDegrees) * pressure;
    }
    if (!MATH::IsFinite(safeTarget) || MATH::Length(safeTarget - safeEye) <= kViewEpsilon) {
        safeTarget = primaryPivot;
    }
    camera.SetPerspective(verticalFov, camera.GetAspect(), camera.GetNearZ(), camera.GetFarZ());
    camera.SetLookAt(safeEye, safeTarget, camera.GetUp());
    finish();
    return true;
}

} // namespace CAMERA

} // namespace HIKARI
