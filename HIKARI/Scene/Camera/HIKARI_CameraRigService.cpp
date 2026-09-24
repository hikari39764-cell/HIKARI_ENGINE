#include "Scene/Camera/HIKARI_CameraRigService.h"

#include <algorithm>
#include <cmath>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Render3D/Core/HIKARI_Camera3D.h"

namespace HIKARI::CAMERA {

namespace {
constexpr float kViewEpsilon = 1.0e-5f;
constexpr float kMinVerticalFov = 1.0e-3f;
constexpr float kMaxVerticalFov = 3.13f;
constexpr float kTwoPi = 6.28318530717958647692f;

bool IsPreferred(int candidatePriority, CameraRigSourceId candidateSource, int currentPriority,
                 CameraRigSourceId currentSource) noexcept {
    return candidatePriority > currentPriority ||
           (candidatePriority == currentPriority && candidateSource < currentSource);
}

MATH::Vec3 Scale(const MATH::Vec3& value, float amount) noexcept { return value * amount; }
} // namespace

void CameraRigService::BeginFrame(uint64_t frameIndex) noexcept {
    if (frameIndex_ == frameIndex) {
        return;
    }
    frameIndex_ = frameIndex;
    poses_.clear();
    modes_.clear();
    modifiers_.clear();
    targetFramings_.clear();
}

void CameraRigService::Clear() noexcept {
    frameIndex_ = 0u;
    poses_.clear();
    modes_.clear();
    modifiers_.clear();
    targetFramings_.clear();
}

bool CameraRigService::SubmitMode(const CameraModeSubmission& submission) {
    if (!submission.cameraObjectId.value || !submission.sourceId || submission.mode.empty() ||
        submission.mode.size() > 128)
        return false;
    const auto found = std::find_if(modes_.begin(), modes_.end(), [&](const auto& entry) {
        return entry.cameraObjectId == submission.cameraObjectId && entry.sourceId == submission.sourceId;
    });
    if (found != modes_.end())
        *found = submission;
    else
        modes_.push_back(submission);
    return true;
}
const CameraModeSubmission* CameraRigService::ResolveMode(SceneObjectId camera) const noexcept {
    const CameraModeSubmission* winner = nullptr;
    for (const auto& entry : modes_)
        if (entry.cameraObjectId == camera &&
            (!winner || IsPreferred(entry.priority, entry.sourceId, winner->priority, winner->sourceId)))
            winner = &entry;
    return winner;
}

bool CameraRigService::SubmitPose(const CameraRigSubmission& submission) {
    if (submission.cameraObjectId.value == 0u || submission.sourceId == 0u || !IsValidPose(submission.pose)) {
        return false;
    }

    const auto existing =
        std::find_if(poses_.begin(), poses_.end(), [&submission](const CameraRigSubmission& candidate) {
            return candidate.cameraObjectId == submission.cameraObjectId && candidate.sourceId == submission.sourceId;
        });
    if (existing != poses_.end()) {
        *existing = submission;
    } else {
        poses_.push_back(submission);
    }
    return true;
}

bool CameraRigService::SubmitModifier(const CameraModifierSubmission& submission) {
    if (submission.sourceId == 0u || !IsValidModifier(submission)) {
        return false;
    }

    const auto existing =
        std::find_if(modifiers_.begin(), modifiers_.end(), [&submission](const CameraModifierSubmission& candidate) {
            return candidate.cameraObjectId == submission.cameraObjectId && candidate.sourceId == submission.sourceId &&
                   candidate.channelId == submission.channelId;
        });
    if (existing != modifiers_.end()) {
        *existing = submission;
    } else {
        modifiers_.push_back(submission);
    }
    return true;
}

bool CameraRigService::SubmitTargetFraming(const CameraTargetFramingSubmission& submission) {
    if (submission.sourceId == 0u || !submission.targetObject.IsValid() || !IsValidTargetFraming(submission)) {
        return false;
    }

    const auto existing = std::find_if(
        targetFramings_.begin(), targetFramings_.end(), [&submission](const CameraTargetFramingSubmission& candidate) {
            return candidate.cameraObjectId == submission.cameraObjectId && candidate.sourceId == submission.sourceId;
        });
    if (existing != targetFramings_.end()) {
        *existing = submission;
    } else {
        targetFramings_.push_back(submission);
    }
    return true;
}

bool CameraRigService::TryResolvePose(SceneObjectId cameraObjectId, CameraRigSubmission& outSubmission) const noexcept {
    const CameraRigSubmission* winner = nullptr;
    for (const CameraRigSubmission& submission : poses_) {
        if (!(submission.cameraObjectId == cameraObjectId)) {
            continue;
        }
        if (winner == nullptr ||
            IsPreferred(submission.priority, submission.sourceId, winner->priority, winner->sourceId)) {
            winner = &submission;
        }
    }
    if (winner == nullptr) {
        return false;
    }
    outSubmission = *winner;
    return true;
}

bool CameraRigService::TryResolveTargetFraming(SceneObjectId cameraObjectId,
                                               CameraTargetFramingSubmission& outSubmission) const noexcept {
    const CameraTargetFramingSubmission* winner = nullptr;
    for (const CameraTargetFramingSubmission& submission : targetFramings_) {
        if (submission.cameraObjectId.value != 0u && !(submission.cameraObjectId == cameraObjectId)) {
            continue;
        }
        if (winner == nullptr ||
            IsPreferred(submission.priority, submission.sourceId, winner->priority, winner->sourceId)) {
            winner = &submission;
        }
    }
    if (winner == nullptr) {
        return false;
    }
    outSubmission = *winner;
    return true;
}

bool CameraRigService::TryResolveFallbackCamera(SceneObjectId& outCameraObjectId) const noexcept {
    const CameraRigSubmission* winner = nullptr;
    for (const CameraRigSubmission& submission : poses_) {
        if (winner == nullptr ||
            IsPreferred(submission.priority, submission.sourceId, winner->priority, winner->sourceId) ||
            (submission.priority == winner->priority && submission.sourceId == winner->sourceId &&
             submission.cameraObjectId.value < winner->cameraObjectId.value)) {
            winner = &submission;
        }
    }
    if (winner == nullptr) {
        outCameraObjectId = {};
        return false;
    }
    outCameraObjectId = winner->cameraObjectId;
    return true;
}

void CameraRigService::ApplyModifiers(SceneObjectId cameraObjectId, bool duringOverride, Camera3D& camera,
                                      float weightScale) const {
    const float resolvedWeightScale = std::clamp(std::isfinite(weightScale) ? weightScale : 1.0f, 0.0f, 1.0f);

    std::vector<const CameraModifierSubmission*> applicable;
    applicable.reserve(modifiers_.size());
    for (const CameraModifierSubmission& modifier : modifiers_) {
        if (modifier.cameraObjectId.value != 0u && !(modifier.cameraObjectId == cameraObjectId)) {
            continue;
        }
        if (duringOverride && !modifier.applyDuringOverride) {
            continue;
        }
        applicable.push_back(&modifier);
    }
    std::stable_sort(applicable.begin(), applicable.end(),
                     [](const CameraModifierSubmission* lhs, const CameraModifierSubmission* rhs) {
                         if (lhs->priority != rhs->priority) {
                             return lhs->priority < rhs->priority;
                         }
                         if (lhs->sourceId != rhs->sourceId) {
                             return lhs->sourceId < rhs->sourceId;
                         }
                         return lhs->channelId < rhs->channelId;
                     });

    MATH::Vec3 eye = camera.GetPosition();
    MATH::Vec3 target = camera.GetTarget();
    MATH::Vec3 up = MATH::Normalize(camera.GetUp());
    MATH::Vec3 forward = MATH::Normalize(target - eye);
    if (MATH::Length(forward) <= kViewEpsilon) {
        forward = {0.0f, 0.0f, 1.0f};
    }
    if (MATH::Length(up) <= kViewEpsilon || std::abs(MATH::Dot(forward, up)) >= 0.999f) {
        up = {0.0f, 1.0f, 0.0f};
    }
    auto basis = MATH::BuildViewBasisRH(forward, up);
    MATH::Vec3 right = basis.right;
    up = basis.up;

    float verticalFov = camera.GetFovYRad();
    float localRoll = 0.0f;
    for (const CameraModifierSubmission* modifier : applicable) {
        const float weight = std::clamp(modifier->weight, 0.0f, 1.0f) * resolvedWeightScale;
        const auto toWorld = [right, up, forward](const MATH::Vec3& local) {
            return right * local.x + up * local.y + forward * local.z;
        };
        eye =
            eye + Scale(modifier->worldPositionOffset, weight) + Scale(toWorld(modifier->localPositionOffset), weight);
        target =
            target + Scale(modifier->worldTargetOffset, weight) + Scale(toWorld(modifier->localTargetOffset), weight);
        verticalFov += modifier->verticalFovOffsetRadians * weight;
        localRoll = std::remainder(localRoll + std::remainder(modifier->localRollRadians, kTwoPi) * weight, kTwoPi);
    }

    // Roll is an explicit presentation modifier. Rebuild the final view
    // basis after all position/target offsets, then rotate its up vector
    // around the resulting forward axis.
    forward = MATH::Normalize(target - eye);
    if (MATH::Length(forward) <= kViewEpsilon) {
        forward = {0.0f, 0.0f, 1.0f};
    }
    basis = MATH::BuildViewBasisRH(forward, up);
    right = basis.right;
    up = basis.up;
    if (std::abs(localRoll) > kViewEpsilon) {
        up = MATH::Normalize(up * std::cos(localRoll) + right * std::sin(localRoll));
    }

    verticalFov = std::clamp(verticalFov, kMinVerticalFov, kMaxVerticalFov);
    if (!camera.IsOrthographic())
        camera.SetPerspective(verticalFov, camera.GetAspect(), camera.GetNearZ(), camera.GetFarZ());
    camera.SetLookAt(eye, target, up);
}

CameraRigRuntimeStatus CameraRigService::GetStatus(SceneObjectId cameraObjectId) const noexcept {
    CameraRigRuntimeStatus status{};
    status.frameIndex = frameIndex_;
    status.cameraObjectId = cameraObjectId;
    CameraRigSubmission pose{};
    if (TryResolvePose(cameraObjectId, pose)) {
        status.sourceId = pose.sourceId;
        status.priority = pose.priority;
        status.poseDriven = true;
    }
    status.modifierCount = static_cast<size_t>(
        std::count_if(modifiers_.begin(), modifiers_.end(), [cameraObjectId](const CameraModifierSubmission& modifier) {
            return modifier.cameraObjectId.value == 0u || modifier.cameraObjectId == cameraObjectId;
        }));
    return status;
}

bool CameraRigService::IsValidPose(const CameraRigPose& pose) noexcept {
    if (!MATH::IsFinite(pose.eye) || !MATH::IsFinite(pose.target) || !MATH::IsFinite(pose.up) ||
        MATH::Length(pose.target - pose.eye) <= kViewEpsilon || MATH::Length(pose.up) <= kViewEpsilon ||
        !std::isfinite(pose.modifierWeightScale) || pose.modifierWeightScale < 0.0f ||
        pose.modifierWeightScale > 1.0f) {
        return false;
    }
    if (pose.overrideClipPlanes && (!std::isfinite(pose.nearClip) || !std::isfinite(pose.farClip) ||
                                    pose.nearClip < 0.001f || pose.farClip <= pose.nearClip))
        return false;
    return !pose.overrideVerticalFov ||
           (std::isfinite(pose.verticalFovRadians) && pose.verticalFovRadians >= kMinVerticalFov &&
            pose.verticalFovRadians <= kMaxVerticalFov);
}

bool CameraRigService::IsValidModifier(const CameraModifierSubmission& modifier) noexcept {
    return MATH::IsFinite(modifier.worldPositionOffset) && MATH::IsFinite(modifier.localPositionOffset) &&
           MATH::IsFinite(modifier.worldTargetOffset) && MATH::IsFinite(modifier.localTargetOffset) &&
           std::isfinite(modifier.verticalFovOffsetRadians) && std::isfinite(modifier.localRollRadians) &&
           std::isfinite(modifier.weight);
}

bool CameraRigService::IsValidTargetFraming(const CameraTargetFramingSubmission& submission) noexcept {
    return MATH::IsFinite(submission.targetPivotOffset) && std::isfinite(submission.targetWeight) &&
           submission.targetWeight >= 0.0f && submission.targetWeight <= 1.0f &&
           std::isfinite(submission.yawTrackingSpeedDegreesPerSecond) &&
           submission.yawTrackingSpeedDegreesPerSecond >= 0.0f && std::isfinite(submission.preferredPitchDegrees) &&
           std::isfinite(submission.pitchTrackingSpeedDegreesPerSecond) &&
           submission.pitchTrackingSpeedDegreesPerSecond >= 0.0f && std::isfinite(submission.distanceScale) &&
           submission.distanceScale >= 0.0f && std::isfinite(submission.maximumDistanceOffset) &&
           submission.maximumDistanceOffset >= 0.0f && std::isfinite(submission.weight) && submission.weight >= 0.0f &&
           submission.weight <= 1.0f && std::isfinite(submission.dynamics.enterSmoothSeconds) &&
           submission.dynamics.enterSmoothSeconds >= 0.0f && std::isfinite(submission.dynamics.exitSmoothSeconds) &&
           submission.dynamics.exitSmoothSeconds >= 0.0f &&
           std::isfinite(submission.dynamics.targetSwitchSmoothSeconds) &&
           submission.dynamics.targetSwitchSmoothSeconds >= 0.0f &&
           std::isfinite(submission.dynamics.yawSmoothSeconds) && submission.dynamics.yawSmoothSeconds >= 0.0f &&
           std::isfinite(submission.dynamics.pitchSmoothSeconds) && submission.dynamics.pitchSmoothSeconds >= 0.0f &&
           std::isfinite(submission.dynamics.focusSmoothSeconds) && submission.dynamics.focusSmoothSeconds >= 0.0f &&
           std::isfinite(submission.dynamics.distanceExpandSmoothSeconds) &&
           submission.dynamics.distanceExpandSmoothSeconds >= 0.0f &&
           std::isfinite(submission.dynamics.distanceContractSmoothSeconds) &&
           submission.dynamics.distanceContractSmoothSeconds >= 0.0f &&
           std::isfinite(submission.dynamics.yawDeadZoneDegrees) && submission.dynamics.yawDeadZoneDegrees >= 0.0f &&
           submission.dynamics.yawDeadZoneDegrees <= 180.0f && std::isfinite(submission.relationshipPreferredSide) &&
           submission.relationshipPreferredSide >= -1.0f && submission.relationshipPreferredSide <= 1.0f &&
           std::isfinite(submission.relationshipShoulderAngleDegrees) &&
           submission.relationshipShoulderAngleDegrees >= 0.0f &&
           submission.relationshipShoulderAngleDegrees <= 75.0f &&
           std::isfinite(submission.relationshipVerticalFovOffsetRadians) &&
           std::abs(submission.relationshipVerticalFovOffsetRadians) <= 1.5f &&
           std::isfinite(submission.relationshipRollRadians) &&
           std::abs(submission.relationshipRollRadians) <= 3.14159265f &&
           std::isfinite(submission.relationshipSideClearanceBias) &&
           submission.relationshipSideClearanceBias >= 0.0f && submission.relationshipSideClearanceBias <= 5.0f &&
           std::isfinite(submission.relationshipDistanceOffset) &&
           std::abs(submission.relationshipDistanceOffset) <= 10.0f &&
           std::isfinite(submission.relationshipPivotHeightOffset) &&
           std::abs(submission.relationshipPivotHeightOffset) <= 5.0f &&
           std::isfinite(submission.relationshipFocusLateralOffset) &&
           std::abs(submission.relationshipFocusLateralOffset) <= 5.0f &&
           std::isfinite(submission.relationshipFocusHeightOffset) &&
           std::abs(submission.relationshipFocusHeightOffset) <= 5.0f;
}

} // namespace HIKARI::CAMERA
