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

        bool IsPreferred(
            int candidatePriority,
            CameraRigSourceId candidateSource,
            int currentPriority,
            CameraRigSourceId currentSource) noexcept {

            return candidatePriority > currentPriority ||
                (candidatePriority == currentPriority &&
                    candidateSource < currentSource);
        }

        MATH::Vec3 Scale(const MATH::Vec3& value, float amount) noexcept {
            return value * amount;
        }
    }

    void CameraRigService::BeginFrame(uint64_t frameIndex) noexcept {
        if (frameIndex_ == frameIndex) {
            return;
        }
        frameIndex_ = frameIndex;
        poses_.clear();
        modifiers_.clear();
    }

    void CameraRigService::Clear() noexcept {
        frameIndex_ = 0u;
        poses_.clear();
        modifiers_.clear();
    }

    bool CameraRigService::SubmitPose(
        const CameraRigSubmission& submission) {

        if (submission.cameraObjectId.value == 0u ||
            submission.sourceId == 0u ||
            !IsValidPose(submission.pose)) {
            return false;
        }

        const auto existing = std::find_if(
            poses_.begin(),
            poses_.end(),
            [&submission](const CameraRigSubmission& candidate) {
                return candidate.cameraObjectId ==
                        submission.cameraObjectId &&
                    candidate.sourceId == submission.sourceId;
            });
        if (existing != poses_.end()) {
            *existing = submission;
        } else {
            poses_.push_back(submission);
        }
        return true;
    }

    bool CameraRigService::SubmitModifier(
        const CameraModifierSubmission& submission) {

        if (submission.sourceId == 0u ||
            !IsValidModifier(submission)) {
            return false;
        }

        const auto existing = std::find_if(
            modifiers_.begin(),
            modifiers_.end(),
            [&submission](const CameraModifierSubmission& candidate) {
                return candidate.cameraObjectId ==
                        submission.cameraObjectId &&
                    candidate.sourceId == submission.sourceId;
            });
        if (existing != modifiers_.end()) {
            *existing = submission;
        } else {
            modifiers_.push_back(submission);
        }
        return true;
    }

    bool CameraRigService::TryResolvePose(
        SceneObjectId cameraObjectId,
        CameraRigSubmission& outSubmission) const noexcept {

        const CameraRigSubmission* winner = nullptr;
        for (const CameraRigSubmission& submission : poses_) {
            if (!(submission.cameraObjectId == cameraObjectId)) {
                continue;
            }
            if (winner == nullptr ||
                IsPreferred(
                    submission.priority,
                    submission.sourceId,
                    winner->priority,
                    winner->sourceId)) {
                winner = &submission;
            }
        }
        if (winner == nullptr) {
            return false;
        }
        outSubmission = *winner;
        return true;
    }

    bool CameraRigService::TryResolveFallbackCamera(
        SceneObjectId& outCameraObjectId) const noexcept {

        const CameraRigSubmission* winner = nullptr;
        for (const CameraRigSubmission& submission : poses_) {
            if (winner == nullptr ||
                IsPreferred(
                    submission.priority,
                    submission.sourceId,
                    winner->priority,
                    winner->sourceId) ||
                (submission.priority == winner->priority &&
                    submission.sourceId == winner->sourceId &&
                    submission.cameraObjectId.value <
                        winner->cameraObjectId.value)) {
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

    void CameraRigService::ApplyModifiers(
        SceneObjectId cameraObjectId,
        bool duringOverride,
        Camera3D& camera) const {

        std::vector<const CameraModifierSubmission*> applicable;
        applicable.reserve(modifiers_.size());
        for (const CameraModifierSubmission& modifier : modifiers_) {
            if (modifier.cameraObjectId.value != 0u &&
                !(modifier.cameraObjectId == cameraObjectId)) {
                continue;
            }
            if (duringOverride && !modifier.applyDuringOverride) {
                continue;
            }
            applicable.push_back(&modifier);
        }
        std::stable_sort(
            applicable.begin(),
            applicable.end(),
            [](const CameraModifierSubmission* lhs,
                const CameraModifierSubmission* rhs) {
                if (lhs->priority != rhs->priority) {
                    return lhs->priority < rhs->priority;
                }
                return lhs->sourceId < rhs->sourceId;
            });

        MATH::Vec3 eye = camera.GetPosition();
        MATH::Vec3 target = camera.GetTarget();
        MATH::Vec3 up = MATH::Normalize(camera.GetUp());
        MATH::Vec3 forward = MATH::Normalize(target - eye);
        if (MATH::Length(forward) <= kViewEpsilon) {
            forward = { 0.0f, 0.0f, 1.0f };
        }
        if (MATH::Length(up) <= kViewEpsilon ||
            std::abs(MATH::Dot(forward, up)) >= 0.999f) {
            up = { 0.0f, 1.0f, 0.0f };
        }
        MATH::Vec3 right = MATH::Normalize(MATH::Cross(up, forward));
        if (MATH::Length(right) <= kViewEpsilon) {
            right = { 1.0f, 0.0f, 0.0f };
        }
        up = MATH::Normalize(MATH::Cross(forward, right));

        float verticalFov = camera.GetFovYRad();
        for (const CameraModifierSubmission* modifier : applicable) {
            const float weight = std::clamp(modifier->weight, 0.0f, 1.0f);
            const auto toWorld = [right, up, forward](
                const MATH::Vec3& local) {
                return right * local.x + up * local.y +
                    forward * local.z;
            };
            eye = eye + Scale(modifier->worldPositionOffset, weight) +
                Scale(toWorld(modifier->localPositionOffset), weight);
            target = target + Scale(modifier->worldTargetOffset, weight) +
                Scale(toWorld(modifier->localTargetOffset), weight);
            verticalFov += modifier->verticalFovOffsetRadians * weight;
        }

        verticalFov = std::clamp(
            verticalFov,
            kMinVerticalFov,
            kMaxVerticalFov);
        camera.SetPerspective(
            verticalFov,
            camera.GetAspect(),
            camera.GetNearZ(),
            camera.GetFarZ());
        camera.SetLookAt(eye, target, up);
    }

    CameraRigRuntimeStatus CameraRigService::GetStatus(
        SceneObjectId cameraObjectId) const noexcept {

        CameraRigRuntimeStatus status{};
        status.frameIndex = frameIndex_;
        status.cameraObjectId = cameraObjectId;
        CameraRigSubmission pose{};
        if (TryResolvePose(cameraObjectId, pose)) {
            status.sourceId = pose.sourceId;
            status.priority = pose.priority;
            status.poseDriven = true;
        }
        status.modifierCount = static_cast<size_t>(std::count_if(
            modifiers_.begin(),
            modifiers_.end(),
            [cameraObjectId](const CameraModifierSubmission& modifier) {
                return modifier.cameraObjectId.value == 0u ||
                    modifier.cameraObjectId == cameraObjectId;
            }));
        return status;
    }

    bool CameraRigService::IsValidPose(
        const CameraRigPose& pose) noexcept {

        if (!MATH::IsFinite(pose.eye) || !MATH::IsFinite(pose.target) ||
            !MATH::IsFinite(pose.up) ||
            MATH::Length(pose.target - pose.eye) <= kViewEpsilon ||
            MATH::Length(pose.up) <= kViewEpsilon) {
            return false;
        }
        return !pose.overrideVerticalFov ||
            (std::isfinite(pose.verticalFovRadians) &&
                pose.verticalFovRadians >= kMinVerticalFov &&
                pose.verticalFovRadians <= kMaxVerticalFov);
    }

    bool CameraRigService::IsValidModifier(
        const CameraModifierSubmission& modifier) noexcept {

        return MATH::IsFinite(modifier.worldPositionOffset) &&
            MATH::IsFinite(modifier.localPositionOffset) &&
            MATH::IsFinite(modifier.worldTargetOffset) &&
            MATH::IsFinite(modifier.localTargetOffset) &&
            std::isfinite(modifier.verticalFovOffsetRadians) &&
            std::isfinite(modifier.weight);
    }

} // namespace HIKARI::CAMERA
