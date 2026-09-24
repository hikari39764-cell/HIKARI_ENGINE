#include "HIKARI/Project/ActionViews.h"
#include "HIKARI/Project/WorldView.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectActionValidation.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Scene/Components/HIKARI_CameraFlowComponent.h"
#include "Scene/Camera/HIKARI_CameraRigService.h"
#include "Scene/HIKARI_CameraDirector.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Runtime/Presentation/HIKARI_GamePresentationService.h"
#include "Vfx/Post/HIKARI_PostSystem.h"
#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace HIKARI {
using namespace PROJECT::INTERNAL;
namespace {
bool CopyCameraPose(const Camera3D& camera, GameProjectCameraPose& outPose) noexcept {
    const MATH::Vec3 eye = camera.GetPosition();
    const MATH::Vec3 target = camera.GetTarget();
    const MATH::Vec3 up = camera.GetUp();
    const float fov = camera.GetFovYRad();
    const float aspect = camera.GetAspect();
    if (!MATH::IsFinite(eye) || !MATH::IsFinite(target) || !MATH::IsFinite(up) || !std::isfinite(fov) ||
        !std::isfinite(aspect) || aspect <= 0.0f || MATH::Length(target - eye) <= kDirectionEpsilon ||
        MATH::Length(up) <= kDirectionEpsilon ||
        std::abs(MATH::Dot(MATH::Normalize(target - eye), MATH::Normalize(up))) >= 0.999f) {
        return false;
    }
    outPose.eye = ToProject(eye);
    outPose.target = ToProject(target);
    outPose.up = ToProject(up);
    outPose.verticalFovRadians = fov;
    outPose.aspectRatio = aspect;
    return true;
}

bool ToCameraObjectId(World& world, uint64_t runtimeHandle, SceneObjectId& outObjectId) noexcept {
    outObjectId = {};
    if (runtimeHandle == 0u) {
        return true;
    }
    GameObject* object = FindObject(&world, runtimeHandle);
    if (object == nullptr) {
        return false;
    }
    outObjectId = object->GetDocumentId();
    return outObjectId.value != 0u;
}

bool BuildCameraTargetFramingSubmission(World& world, const GameProjectCameraTargetFramingRequest& request,
                                        const GameProjectCameraTargetFramingDynamics& dynamics,
                                        CAMERA::CameraTargetFramingSubmission& submission) noexcept {
    if (request.sourceId == 0u || request.targetObjectHandle == 0u || !IsFinite(request.targetPivotOffset) ||
        !std::isfinite(request.targetWeight) || request.targetWeight < 0.0f || request.targetWeight > 1.0f ||
        !std::isfinite(request.yawTrackingSpeedDegreesPerSecond) || request.yawTrackingSpeedDegreesPerSecond < 0.0f ||
        !std::isfinite(request.preferredPitchDegrees) || !std::isfinite(request.pitchTrackingSpeedDegreesPerSecond) ||
        request.pitchTrackingSpeedDegreesPerSecond < 0.0f || !std::isfinite(request.distanceScale) ||
        request.distanceScale < 0.0f || !std::isfinite(request.maximumDistanceOffset) ||
        request.maximumDistanceOffset < 0.0f || !std::isfinite(request.weight) || request.weight < 0.0f ||
        request.weight > 1.0f || !std::isfinite(dynamics.enterSmoothSeconds) || dynamics.enterSmoothSeconds < 0.0f ||
        !std::isfinite(dynamics.exitSmoothSeconds) || dynamics.exitSmoothSeconds < 0.0f ||
        !std::isfinite(dynamics.targetSwitchSmoothSeconds) || dynamics.targetSwitchSmoothSeconds < 0.0f ||
        !std::isfinite(dynamics.yawSmoothSeconds) || dynamics.yawSmoothSeconds < 0.0f ||
        !std::isfinite(dynamics.pitchSmoothSeconds) || dynamics.pitchSmoothSeconds < 0.0f ||
        !std::isfinite(dynamics.focusSmoothSeconds) || dynamics.focusSmoothSeconds < 0.0f ||
        !std::isfinite(dynamics.distanceExpandSmoothSeconds) || dynamics.distanceExpandSmoothSeconds < 0.0f ||
        !std::isfinite(dynamics.distanceContractSmoothSeconds) || dynamics.distanceContractSmoothSeconds < 0.0f ||
        !std::isfinite(dynamics.yawDeadZoneDegrees) || dynamics.yawDeadZoneDegrees < 0.0f ||
        dynamics.yawDeadZoneDegrees > 180.0f) {
        return false;
    }

    GameObject* target = FindObject(&world, request.targetObjectHandle);
    if (target == nullptr || !ToCameraObjectId(world, request.cameraObjectHandle, submission.cameraObjectId)) {
        return false;
    }
    submission.targetObject = target->GetRuntimeHandle();
    submission.sourceId = request.sourceId;
    submission.priority = request.priority;
    submission.targetPivotOffset = ToRuntime(request.targetPivotOffset);
    submission.targetWeight = request.targetWeight;
    submission.yawTrackingSpeedDegreesPerSecond = request.yawTrackingSpeedDegreesPerSecond;
    submission.preferredPitchDegrees = request.preferredPitchDegrees;
    submission.pitchTrackingSpeedDegreesPerSecond = request.pitchTrackingSpeedDegreesPerSecond;
    submission.distanceScale = request.distanceScale;
    submission.maximumDistanceOffset = request.maximumDistanceOffset;
    submission.weight = request.weight;
    submission.suppressOrbitInput = request.suppressOrbitInput != 0u;
    submission.suppressZoomInput = request.suppressZoomInput != 0u;
    submission.dynamics.enterSmoothSeconds = dynamics.enterSmoothSeconds;
    submission.dynamics.exitSmoothSeconds = dynamics.exitSmoothSeconds;
    submission.dynamics.targetSwitchSmoothSeconds = dynamics.targetSwitchSmoothSeconds;
    submission.dynamics.yawSmoothSeconds = dynamics.yawSmoothSeconds;
    submission.dynamics.pitchSmoothSeconds = dynamics.pitchSmoothSeconds;
    submission.dynamics.focusSmoothSeconds = dynamics.focusSmoothSeconds;
    submission.dynamics.distanceExpandSmoothSeconds = dynamics.distanceExpandSmoothSeconds;
    submission.dynamics.distanceContractSmoothSeconds = dynamics.distanceContractSmoothSeconds;
    submission.dynamics.yawDeadZoneDegrees = dynamics.yawDeadZoneDegrees;
    return true;
}
} // namespace

CameraView GameProjectSdkAccess::MakeCameraView(World& world) noexcept { return CameraView(&world); }

CameraView WorldView::GetCamera() const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? GameProjectSdkAccess::MakeCameraView(*world) : CameraView{};
}

bool CameraView::SubmitPose(const GameProjectCameraPoseRequest& request) const noexcept {
    try {
        if (!world_ || !request.cameraObjectHandle || !request.sourceId ||
            !std::isfinite(request.modifierWeightScale) || request.modifierWeightScale < 0.0f ||
            request.modifierWeightScale > 1.0f || request.pose.reserved)
            return false;
        auto* world = static_cast<World*>(world_);
        if (!world->IsOwnerThread() || GameProjectSdkAccess::IsWorldClosing(*world))
            return false;
        auto* service = world->Services().Find<CAMERA::CameraRigService>();
        if (!service)
            return false;
        CAMERA::CameraRigSubmission submission{};
        if (!ToCameraObjectId(*world, request.cameraObjectHandle, submission.cameraObjectId))
            return false;
        submission.sourceId = request.sourceId;
        submission.priority = request.priority;
        submission.pose.eye = ToRuntime(request.pose.eye);
        submission.pose.target = ToRuntime(request.pose.target);
        submission.pose.up = ToRuntime(request.pose.up);
        submission.pose.overrideVerticalFov = true;
        submission.pose.verticalFovRadians = request.pose.verticalFovRadians;
        submission.pose.modifierWeightScale = request.modifierWeightScale;
        return service->SubmitPose(submission);
    } catch (...) {
        return false;
    }
}

bool CameraView::SubmitModifier(const GameProjectCameraModifier& modifier) const noexcept {
    try {
        if (world_ == nullptr || modifier.sourceId == 0u || !IsFinite(modifier.worldPositionOffset) ||
            !IsFinite(modifier.localPositionOffset) || !IsFinite(modifier.worldTargetOffset) ||
            !IsFinite(modifier.localTargetOffset) || !std::isfinite(modifier.verticalFovOffsetRadians) ||
            !std::isfinite(modifier.weight)) {
            return false;
        }
        World* world = static_cast<World*>(world_);
        CAMERA::CameraRigService* service = world->Services().Find<CAMERA::CameraRigService>();
        if (service == nullptr) {
            return false;
        }
        CAMERA::CameraModifierSubmission submission{};
        if (!ToCameraObjectId(*world, modifier.cameraObjectHandle, submission.cameraObjectId)) {
            return false;
        }
        submission.sourceId = modifier.sourceId;
        submission.priority = modifier.priority;
        submission.worldPositionOffset = ToRuntime(modifier.worldPositionOffset);
        submission.localPositionOffset = ToRuntime(modifier.localPositionOffset);
        submission.worldTargetOffset = ToRuntime(modifier.worldTargetOffset);
        submission.localTargetOffset = ToRuntime(modifier.localTargetOffset);
        submission.verticalFovOffsetRadians = modifier.verticalFovOffsetRadians;
        submission.weight = modifier.weight;
        submission.applyDuringOverride = modifier.applyDuringOverride != 0u;
        return service->SubmitModifier(submission);
    } catch (...) {
        return false;
    }
}

bool CameraView::SubmitRollModifier(const GameProjectCameraRollModifier& modifier) const noexcept {
    try {
        if (world_ == nullptr || modifier.sourceId == 0u || !std::isfinite(modifier.localRollRadians) ||
            !std::isfinite(modifier.weight)) {
            return false;
        }
        World* world = static_cast<World*>(world_);
        CAMERA::CameraRigService* service = world->Services().Find<CAMERA::CameraRigService>();
        if (service == nullptr) {
            return false;
        }
        CAMERA::CameraModifierSubmission submission{};
        if (!ToCameraObjectId(*world, modifier.cameraObjectHandle, submission.cameraObjectId)) {
            return false;
        }
        submission.sourceId = modifier.sourceId;
        submission.channelId = 1u;
        submission.priority = modifier.priority;
        submission.localRollRadians = modifier.localRollRadians;
        submission.weight = modifier.weight;
        submission.applyDuringOverride = modifier.applyDuringOverride != 0u;
        return service->SubmitModifier(submission);
    } catch (...) {
        return false;
    }
}

bool CameraView::GetControlPose(GameProjectCameraPose& outPose) const noexcept {
    outPose = {};
    try {
        if (world_ == nullptr) {
            return false;
        }
        World* world = static_cast<World*>(world_);
        GameplayCameraService* service = world->Services().Find<GameplayCameraService>();
        if (service == nullptr || service->camera == nullptr || !service->IsRuntimeCameraActive()) {
            return false;
        }
        const Camera3D& camera = *service->camera;
        return CopyCameraPose(camera, outPose);
    } catch (...) {
        outPose = {};
        return false;
    }
}

bool CameraView::GetRenderedPose(GameProjectCameraPose& outPose) const noexcept {
    outPose = {};
    try {
        if (world_ == nullptr)
            return false;
        World* world = static_cast<World*>(world_);
        const auto* service = world->Services().Find<GameplayCameraService>();
        const auto* director = world->Services().Find<CameraDirector>();
        if (!service || !service->IsRuntimeCameraActive() || !director)
            return false;
        const auto& frame = director->GetResolvedFrame();
        return frame.valid && CopyCameraPose(frame.camera, outPose);
    } catch (...) {
        outPose = {};
        return false;
    }
}

bool CameraView::GetRadialScreenWarp(float& outCurvature, float& outStrength) const noexcept {
    outCurvature = outStrength = 0.0f;
    try {
        if (world_ == nullptr)
            return false;
        World* world = static_cast<World*>(world_);
        const auto* service = world->Services().Find<GameplayCameraService>();
        if (!service || !service->IsRuntimeCameraActive())
            return false;
        return POST::PostSystem::GetRadialScreenWarp(outCurvature, outStrength);
    } catch (...) {
        outCurvature = outStrength = 0.0f;
        return false;
    }
}

bool CameraView::TriggerFlow(uint64_t handle) const noexcept {
    try {
        auto* world = static_cast<World*>(world_);
        if (!world || !world->IsOwnerThread() || GameProjectSdkAccess::IsWorldClosing(*world))
            return false;
        auto* object = FindObject(world, handle);
        auto* component = object ? object->GetComponent<CameraFlowComponent>() : nullptr;
        return component && component->RequestTrigger();
    } catch (...) {
        return false;
    }
}

bool CameraView::InterruptFlow(uint64_t handle) const noexcept {
    try {
        auto* world = static_cast<World*>(world_);
        if (!world || !world->IsOwnerThread() || GameProjectSdkAccess::IsWorldClosing(*world))
            return false;
        auto* object = FindObject(world, handle);
        auto* component = object ? object->GetComponent<CameraFlowComponent>() : nullptr;
        return component && component->RequestInterrupt();
    } catch (...) {
        return false;
    }
}

bool CameraView::GetFlowStatus(uint64_t handle, GameProjectCameraFlowStatus& out) const noexcept {
    out = {};
    try {
        auto* world = static_cast<World*>(world_);
        if (!world || !world->IsOwnerThread() || GameProjectSdkAccess::IsWorldClosing(*world))
            return false;
        const auto* object = FindObject(world, handle);
        const auto* component = object ? object->GetComponent<CameraFlowComponent>() : nullptr;
        if (!component)
            return false;
        const auto& r = component->Runtime();
        out.stage = static_cast<GameProjectCameraFlowStage>(r.stage);
        out.inside = r.inside;
        out.runs = r.runs;
        out.timeSeconds = r.time;
        out.durationSeconds = r.duration;
        return true;
    } catch (...) {
        return false;
    }
}

bool CameraView::SubmitGameplayMode(uint64_t cameraObjectHandle, std::string_view mode, uint64_t sourceId,
                                    int32_t priority) const noexcept {
    try {
        if (!world_)
            return false;
        auto& world = *static_cast<World*>(world_);
        auto* service = world.Services().Find<CAMERA::CameraRigService>();
        CAMERA::CameraModeSubmission request;
        if (!service || !ToCameraObjectId(world, cameraObjectHandle, request.cameraObjectId))
            return false;
        request.sourceId = sourceId;
        request.priority = priority;
        request.mode = mode;
        return service->SubmitMode(request);
    } catch (...) {
        return false;
    }
}

bool CameraView::IsReady() const noexcept {
    try {
        return world_ != nullptr && RuntimeSceneContext::IsCameraReady(static_cast<World*>(world_));
    } catch (...) {
        return false;
    }
}

bool CameraView::SubmitTargetFraming(const GameProjectCameraTargetFramingRequest& request) const noexcept {
    return SubmitTargetFramingWithDynamics(request, {});
}

bool CameraView::SubmitTargetFramingWithDynamics(
    const GameProjectCameraTargetFramingRequest& request,
    const GameProjectCameraTargetFramingDynamics& dynamics) const noexcept {
    try {
        if (world_ == nullptr) {
            return false;
        }
        World* world = static_cast<World*>(world_);
        CAMERA::CameraRigService* service = world->Services().Find<CAMERA::CameraRigService>();
        CAMERA::CameraTargetFramingSubmission submission{};
        if (service == nullptr || !BuildCameraTargetFramingSubmission(*world, request, dynamics, submission)) {
            return false;
        }
        return service->SubmitTargetFraming(submission);
    } catch (...) {
        return false;
    }
}

bool CameraView::SubmitRelationshipShot(const GameProjectCameraTargetFramingRequest& request,
                                        const GameProjectCameraTargetFramingDynamics& dynamics,
                                        const GameProjectCameraRelationshipShotProfile& profile) const noexcept {
    try {
        if (world_ == nullptr || !std::isfinite(profile.preferredSide) || profile.preferredSide < -1.0f ||
            profile.preferredSide > 1.0f || !std::isfinite(profile.shoulderAngleDegrees) ||
            profile.shoulderAngleDegrees < 0.0f || profile.shoulderAngleDegrees > 75.0f ||
            !std::isfinite(profile.verticalFovOffsetRadians) || std::abs(profile.verticalFovOffsetRadians) > 1.5f ||
            !std::isfinite(profile.rollRadians) || std::abs(profile.rollRadians) > 3.14159265f ||
            !std::isfinite(profile.sideClearanceBias) || profile.sideClearanceBias < 0.0f ||
            profile.sideClearanceBias > 5.0f || !std::isfinite(profile.distanceOffset) ||
            std::abs(profile.distanceOffset) > 10.0f || !std::isfinite(profile.pivotHeightOffset) ||
            std::abs(profile.pivotHeightOffset) > 5.0f || !std::isfinite(profile.focusLateralOffset) ||
            std::abs(profile.focusLateralOffset) > 5.0f || !std::isfinite(profile.focusHeightOffset) ||
            std::abs(profile.focusHeightOffset) > 5.0f || profile.suppressModifiers > 1u || profile.reserved[0] != 0u ||
            profile.reserved[1] != 0u || profile.reserved[2] != 0u) {
            return false;
        }
        World* world = static_cast<World*>(world_);
        CAMERA::CameraRigService* service = world->Services().Find<CAMERA::CameraRigService>();
        CAMERA::CameraTargetFramingSubmission submission{};
        if (service == nullptr || !BuildCameraTargetFramingSubmission(*world, request, dynamics, submission)) {
            return false;
        }
        submission.relationshipShot = true;
        submission.relationshipPreferredSide = profile.preferredSide;
        submission.relationshipShoulderAngleDegrees = profile.shoulderAngleDegrees;
        submission.relationshipVerticalFovOffsetRadians = profile.verticalFovOffsetRadians;
        submission.relationshipRollRadians = profile.rollRadians;
        submission.relationshipSideClearanceBias = profile.sideClearanceBias;
        submission.relationshipDistanceOffset = profile.distanceOffset;
        submission.relationshipPivotHeightOffset = profile.pivotHeightOffset;
        submission.relationshipFocusLateralOffset = profile.focusLateralOffset;
        submission.relationshipFocusHeightOffset = profile.focusHeightOffset;
        submission.relationshipSuppressModifiers = profile.suppressModifiers != 0u;
        return service->SubmitTargetFraming(submission);
    } catch (...) {
        return false;
    }
}
} // namespace HIKARI
