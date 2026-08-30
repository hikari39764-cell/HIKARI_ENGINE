#include "HIKARI_CameraFollowSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

#include "Core/HIKARI_FrameContext.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Gameplay/Motion/HIKARI_MotionIntentService.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "Physics/HIKARI_PhysicsTypes.h"
#include "Physics/HIKARI_PhysicsWorldService.h"
#include "Scene/Camera/HIKARI_CameraRigService.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/HIKARI_CameraFollowComponent.h"
#include "Scene/Components/HIKARI_PhysicsBodyComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_PresentationTransformService.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    namespace {
        constexpr CAMERA::CameraRigSourceId kFollowRigSource =
            CAMERA::MakeCameraRigSourceId(
                "HIKARI.CameraRig.OrbitFollow");
        constexpr float kInputEpsilon = 1.0e-4f;
        constexpr float kViewEpsilon = 1.0e-5f;
        constexpr float kBackwardMovementThreshold = -0.1f;
        constexpr float kDefaultGameplayAspect = 16.0f / 9.0f;
        constexpr float kMinimumGameplayAspect = 0.01f;
        constexpr float kCompositionMinimumDepth = 0.05f;
        constexpr float kCompositionPitchSearchRangeDegrees = 18.0f;
        constexpr float kCompositionTargetWeightFadeStart = 0.30f;
        constexpr float kCompositionTargetWeightFadeEnd = 0.48f;
        constexpr float kCompositionTargetPredictionSeconds = 0.08f;
        constexpr float kCompositionMaximumPredictionDistance = 0.65f;
        constexpr float kFinalCameraEmergencyDistance = 0.05f;
        constexpr float kFinalCameraEmergencyFovDegrees = 10.0f;
        constexpr float kPrimaryVisibilityVerticalOffset = 0.52f;
        constexpr int kCompositionYawSearchSamples = 17;
        constexpr int kCompositionPitchSearchSamples = 13;

        float DegreesToRadians(float degrees) noexcept {
            return degrees * std::numbers::pi_v<float> / 180.0f;
        }

        float RadiansToDegrees(float radians) noexcept {
            return radians * 180.0f / std::numbers::pi_v<float>;
        }

        float SmoothFactor(float smooth, float dt) noexcept {
            if (smooth <= 0.0f || dt <= 0.0f) {
                return 1.0f;
            }
            return std::clamp(
                1.0f - std::exp(-smooth * dt),
                0.0f,
                1.0f);
        }

        float WrapDegrees(float degrees) noexcept {
            float wrapped = std::fmod(degrees + 180.0f, 360.0f);
            if (wrapped < 0.0f) {
                wrapped += 360.0f;
            }
            return wrapped - 180.0f;
        }

        float MoveTowardsAngle(
            float current,
            float target,
            float maxDelta) noexcept {

            const float delta = WrapDegrees(target - current);
            if (std::abs(delta) <= maxDelta) {
                return target;
            }
            return current + std::copysign(maxDelta, delta);
        }

        float Lerp(float from, float to, float amount) noexcept {
            return from + (to - from) * std::clamp(amount, 0.0f, 1.0f);
        }

        float ScreenCompositionPolicyWeight(float targetWeight) noexcept {
            const float normalized = std::clamp(
                (targetWeight - kCompositionTargetWeightFadeStart) /
                    (kCompositionTargetWeightFadeEnd -
                        kCompositionTargetWeightFadeStart),
                0.0f,
                1.0f);
            return normalized * normalized * (3.0f - 2.0f * normalized);
        }

        float SmoothDampScalar(
            float current,
            float target,
            float& currentVelocity,
            float smoothTime,
            float maxSpeed,
            float dt) noexcept {

            if (!std::isfinite(current) || !std::isfinite(target) ||
                !std::isfinite(currentVelocity)) {
                currentVelocity = 0.0f;
                return std::isfinite(current) ? current : 0.0f;
            }
            if (dt <= 0.0f || !std::isfinite(dt)) {
                return current;
            }
            if (!std::isfinite(smoothTime) || smoothTime <= kViewEpsilon) {
                currentVelocity = 0.0f;
                return target;
            }

            smoothTime = (std::max)(smoothTime, kViewEpsilon);
            const float omega = 2.0f / smoothTime;
            const float x = omega * dt;
            const float exp = 1.0f /
                (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
            const float originalTarget = target;
            float change = current - target;
            // A negative maxSpeed means unbounded smoothing.  Zero is a real
            // limit: callers use it to disable a tracking channel.
            if (std::isfinite(maxSpeed) && maxSpeed >= 0.0f) {
                const float maxChange = maxSpeed * smoothTime;
                if (std::isfinite(maxChange)) {
                    change = std::clamp(
                        change,
                        -maxChange,
                        maxChange);
                }
            }
            target = current - change;
            const float temp =
                (currentVelocity + omega * change) * dt;
            currentVelocity =
                (currentVelocity - omega * temp) * exp;
            float output = target + (change + temp) * exp;

            if ((originalTarget - current > 0.0f) ==
                    (output > originalTarget)) {
                output = originalTarget;
                currentVelocity = dt > kViewEpsilon
                    ? (output - originalTarget) / dt
                    : 0.0f;
            }
            if (!std::isfinite(output) ||
                !std::isfinite(currentVelocity)) {
                currentVelocity = 0.0f;
                return originalTarget;
            }
            return output;
        }

        float SmoothDampAngle(
            float current,
            float target,
            float& currentVelocity,
            float smoothTime,
            float maxSpeed,
            float dt) noexcept {

            const float delta = WrapDegrees(target - current);
            return SmoothDampScalar(
                current,
                current + delta,
                currentVelocity,
                smoothTime,
                maxSpeed,
                dt);
        }

        MATH::Vec3 SmoothDampVector(
            const MATH::Vec3& current,
            const MATH::Vec3& target,
            MATH::Vec3& currentVelocity,
            float smoothTime,
            float dt) noexcept {

            MATH::Vec3 result = current;
            result.x = SmoothDampScalar(
                current.x,
                target.x,
                currentVelocity.x,
                smoothTime,
                -1.0f,
                dt);
            result.y = SmoothDampScalar(
                current.y,
                target.y,
                currentVelocity.y,
                smoothTime,
                -1.0f,
                dt);
            result.z = SmoothDampScalar(
                current.z,
                target.z,
                currentVelocity.z,
                smoothTime,
                -1.0f,
                dt);
            return MATH::IsFinite(result) ? result : target;
        }

        MATH::Vec3 ExtractAxis(
            const MATH::Mat4& matrix,
            int column) noexcept {

            return {
                matrix.m[column][0],
                matrix.m[column][1],
                matrix.m[column][2]
            };
        }

        MATH::Vec3 TransformPoint(
            const MATH::Mat4& matrix,
            const MATH::Vec3& point) noexcept {

            const MATH::Vec4 transformed = matrix.TransformPoint({
                point.x,
                point.y,
                point.z,
                1.0f
            });
            return { transformed.x, transformed.y, transformed.z };
        }

        MATH::Vec3 OrbitDirection(
            float yawDegrees,
            float pitchDegrees) noexcept {

            const float yaw = DegreesToRadians(yawDegrees);
            const float pitch = DegreesToRadians(pitchDegrees);
            const float horizontal = std::cos(pitch);
            return MATH::Normalize({
                std::sin(yaw) * horizontal,
                std::sin(pitch),
                -std::cos(yaw) * horizontal
            });
        }

        float ResolveGameplayAspect(
            const GameplayCameraService* cameraService) noexcept {

            if (cameraService != nullptr && cameraService->camera != nullptr) {
                const float aspect = cameraService->camera->GetAspect();
                if (std::isfinite(aspect) &&
                    aspect >= kMinimumGameplayAspect) {
                    return aspect;
                }
            }
            return kDefaultGameplayAspect;
        }

        float ResolveGameplayVerticalFov(
            const GameplayCameraService* cameraService,
            float fallbackVerticalFovRadians) noexcept {

            if (cameraService != nullptr && cameraService->camera != nullptr) {
                const float verticalFovRadians =
                    cameraService->camera->GetFovYRad();
                if (std::isfinite(verticalFovRadians) &&
                    verticalFovRadians >= DegreesToRadians(1.0f) &&
                    verticalFovRadians <= DegreesToRadians(179.0f)) {
                    return verticalFovRadians;
                }
            }
            return fallbackVerticalFovRadians;
        }

        struct ViewportProjection {
            float x = 0.5f;
            float y = 0.5f;
            float depth = 0.0f;
            bool valid = false;
        };

        ViewportProjection ProjectPointToViewport(
            const MATH::Vec3& eye,
            const MATH::Vec3& lookAt,
            const MATH::Vec3& point,
            float verticalFovRadians,
            float aspect) noexcept {

            ViewportProjection result{};
            MATH::Vec3 forward = MATH::Normalize(lookAt - eye);
            if (!MATH::IsFinite(forward) ||
                MATH::Length(forward) <= kViewEpsilon) {
                return result;
            }
            MATH::Vec3 right = MATH::Normalize(MATH::Cross(
                MATH::Vec3{ 0.0f, 1.0f, 0.0f },
                forward));
            if (!MATH::IsFinite(right) ||
                MATH::Length(right) <= kViewEpsilon) {
                right = MATH::Normalize(MATH::Cross(
                    MATH::Vec3{ 1.0f, 0.0f, 0.0f },
                    forward));
            }
            if (!MATH::IsFinite(right) ||
                MATH::Length(right) <= kViewEpsilon) {
                return result;
            }
            const MATH::Vec3 up = MATH::Normalize(MATH::Cross(
                forward,
                right));
            const MATH::Vec3 relative = point - eye;
            const float depth = MATH::Dot(relative, forward);
            const float clampedFov = std::clamp(
                verticalFovRadians,
                DegreesToRadians(1.0f),
                DegreesToRadians(179.0f));
            const float tanHalfFov = std::tan(clampedFov * 0.5f);
            if (!std::isfinite(depth) ||
                depth <= kCompositionMinimumDepth ||
                !std::isfinite(tanHalfFov) ||
                tanHalfFov <= kViewEpsilon ||
                !std::isfinite(aspect) ||
                aspect < kMinimumGameplayAspect) {
                return result;
            }

            const float ndcX = MATH::Dot(relative, right) /
                (depth * tanHalfFov * aspect);
            const float ndcY = MATH::Dot(relative, up) /
                (depth * tanHalfFov);
            if (!std::isfinite(ndcX) || !std::isfinite(ndcY)) {
                return result;
            }
            result.x = ndcX * 0.5f + 0.5f;
            result.y = -ndcY * 0.5f + 0.5f;
            result.depth = depth;
            result.valid = true;
            return result;
        }

        struct ScreenCompositionTargets {
            float primaryX = 0.5f;
            float primaryY = 0.5f;
            float secondaryX = 0.5f;
            float secondaryY = 0.5f;
            float softHalfWidth = 0.075f;
            float softHalfHeight = 0.10f;
            float hardHalfWidth = 0.20f;
            float hardHalfHeight = 0.24f;
        };

        ScreenCompositionTargets BuildCompositionTargets(
            const CameraFollowComponent& component,
            float side,
            float framingWeight,
            float collisionBlend) noexcept {

            const bool secondaryOnRight = side >= 0.0f;
            const float basePrimaryX = secondaryOnRight
                ? component.GetScreenCompositionPrimaryAnchorX()
                : 1.0f - component.GetScreenCompositionPrimaryAnchorX();
            const float baseSecondaryX = secondaryOnRight
                ? component.GetScreenCompositionSecondaryAnchorX()
                : 1.0f - component.GetScreenCompositionSecondaryAnchorX();
            const float safePrimaryX = secondaryOnRight ? 0.45f : 0.55f;
            const float safeSecondaryX = secondaryOnRight ? 0.55f : 0.45f;
            const float weight = std::clamp(framingWeight, 0.0f, 1.0f);
            const float collision = std::clamp(
                collisionBlend *
                    component.GetScreenCompositionCollisionCenteringStrength(),
                0.0f,
                1.0f);

            ScreenCompositionTargets result{};
            const float primaryX = Lerp(0.5f, basePrimaryX, weight);
            const float secondaryX = Lerp(0.5f, baseSecondaryX, weight);
            result.primaryX = Lerp(
                primaryX,
                Lerp(0.5f, safePrimaryX, weight),
                collision);
            result.secondaryX = Lerp(
                secondaryX,
                Lerp(0.5f, safeSecondaryX, weight),
                collision);
            result.primaryY = Lerp(
                Lerp(
                    0.5f,
                    component.GetScreenCompositionPrimaryAnchorY(),
                    weight),
                Lerp(0.5f, 0.54f, weight),
                collision);
            result.secondaryY = Lerp(
                Lerp(
                    0.5f,
                    component.GetScreenCompositionSecondaryAnchorY(),
                    weight),
                Lerp(0.5f, 0.49f, weight),
                collision);
            result.softHalfWidth = std::clamp(
                component.GetScreenCompositionSoftZoneHalfWidth() +
                    0.035f * collision,
                0.0f,
                0.48f);
            result.softHalfHeight = std::clamp(
                component.GetScreenCompositionSoftZoneHalfHeight() +
                    0.030f * collision,
                0.0f,
                0.48f);
            result.hardHalfWidth = std::clamp(
                component.GetScreenCompositionHardZoneHalfWidth() +
                    0.025f * collision,
                result.softHalfWidth + 0.01f,
                0.50f);
            result.hardHalfHeight = std::clamp(
                component.GetScreenCompositionHardZoneHalfHeight() +
                    0.025f * collision,
                result.softHalfHeight + 0.01f,
                0.50f);
            return result;
        }

        float AccumulateZoneCost(
            float value,
            float anchor,
            float softHalfExtent,
            float hardHalfExtent,
            bool& outSoftViolation,
            bool& outHardViolation) noexcept {

            if (!std::isfinite(value)) {
                outSoftViolation = true;
                outHardViolation = true;
                return 1000.0f;
            }
            const float error = std::abs(value - anchor);
            if (error <= softHalfExtent) {
                return 0.0f;
            }
            outSoftViolation = true;
            if (error > hardHalfExtent) {
                outHardViolation = true;
            }
            const float range = (std::max)(
                hardHalfExtent - softHalfExtent,
                0.01f);
            float normalized = (error - softHalfExtent) / range;
            if (normalized > 1.0f) {
                normalized = 1.0f + (normalized - 1.0f) * 2.5f;
            }
            return normalized * normalized;
        }

        struct ScreenCompositionEvaluation {
            ViewportProjection primary{};
            ViewportProjection secondary{};
            float cost = std::numeric_limits<float>::max();
            bool horizontalSoftViolation = true;
            bool horizontalHardViolation = true;
            bool verticalSoftViolation = true;
            bool verticalHardViolation = true;

            bool HasSoftViolation() const noexcept {
                return horizontalSoftViolation || verticalSoftViolation;
            }
            bool HasHardViolation() const noexcept {
                return horizontalHardViolation || verticalHardViolation;
            }
        };

        ScreenCompositionEvaluation EvaluateScreenComposition(
            const MATH::Vec3& primary,
            const MATH::Vec3& secondary,
            const MATH::Vec3& eye,
            const MATH::Vec3& lookAt,
            float verticalFovRadians,
            float aspect,
            const ScreenCompositionTargets& targets) noexcept {

            ScreenCompositionEvaluation result{};
            result.primary = ProjectPointToViewport(
                eye,
                lookAt,
                primary,
                verticalFovRadians,
                aspect);
            result.secondary = ProjectPointToViewport(
                eye,
                lookAt,
                secondary,
                verticalFovRadians,
                aspect);
            if (!result.primary.valid || !result.secondary.valid) {
                return result;
            }

            result.horizontalSoftViolation = false;
            result.horizontalHardViolation = false;
            result.verticalSoftViolation = false;
            result.verticalHardViolation = false;
            float horizontalCost = 0.0f;
            horizontalCost += AccumulateZoneCost(
                result.primary.x,
                targets.primaryX,
                targets.softHalfWidth,
                targets.hardHalfWidth,
                result.horizontalSoftViolation,
                result.horizontalHardViolation);
            horizontalCost += AccumulateZoneCost(
                result.secondary.x,
                targets.secondaryX,
                targets.softHalfWidth,
                targets.hardHalfWidth,
                result.horizontalSoftViolation,
                result.horizontalHardViolation);
            float verticalCost = 0.0f;
            verticalCost += AccumulateZoneCost(
                result.primary.y,
                targets.primaryY,
                targets.softHalfHeight,
                targets.hardHalfHeight,
                result.verticalSoftViolation,
                result.verticalHardViolation);
            verticalCost += AccumulateZoneCost(
                result.secondary.y,
                targets.secondaryY,
                targets.softHalfHeight,
                targets.hardHalfHeight,
                result.verticalSoftViolation,
                result.verticalHardViolation);
            result.cost = horizontalCost + verticalCost * 0.60f;
            return result;
        }

        float SearchCompositionYaw(
            const CameraFollowComponent& component,
            const MATH::Vec3& primary,
            const MATH::Vec3& secondary,
            const MATH::Vec3& lookAt,
            float relationYaw,
            float side,
            float pitch,
            float distance,
            float verticalFovRadians,
            float aspect,
            float framingWeight,
            float collisionBlend,
            const ScreenCompositionTargets& targets) noexcept {

            const float collisionDamping = 1.0f - std::clamp(
                collisionBlend *
                    component.GetScreenCompositionCollisionCenteringStrength() *
                    0.72f,
                0.0f,
                0.90f);
            const float maximumAngle =
                component.GetScreenCompositionMaximumShoulderAngleDegrees() *
                framingWeight * collisionDamping;
            const float preferredAngle = std::min(
                maximumAngle,
                component.GetScreenCompositionPreferredShoulderAngleDegrees() *
                    framingWeight * collisionDamping);
            if (maximumAngle <= kInputEpsilon) {
                return relationYaw;
            }

            float bestYaw = relationYaw + side * preferredAngle;
            float bestCost = std::numeric_limits<float>::max();
            float bestOffset = preferredAngle;
            for (int sample = 0;
                sample < kCompositionYawSearchSamples;
                ++sample) {
                const float amount = static_cast<float>(sample) /
                    static_cast<float>(kCompositionYawSearchSamples - 1);
                const float offset = maximumAngle * amount;
                const float candidateYaw = relationYaw + side * offset;
                const MATH::Vec3 eye = primary + OrbitDirection(
                    candidateYaw,
                    pitch) * distance;
                const ScreenCompositionEvaluation evaluation =
                    EvaluateScreenComposition(
                        primary,
                        secondary,
                        eye,
                        lookAt,
                        verticalFovRadians,
                        aspect,
                        targets);
                const float regularization = maximumAngle > kViewEpsilon
                    ? (offset - preferredAngle) / maximumAngle
                    : 0.0f;
                const float cost = evaluation.cost +
                    regularization * regularization * 0.08f;
                if (cost < bestCost) {
                    bestCost = cost;
                    bestOffset = offset;
                    bestYaw = candidateYaw;
                }
            }

            float step = maximumAngle /
                static_cast<float>(kCompositionYawSearchSamples - 1);
            for (int iteration = 0; iteration < 3; ++iteration) {
                for (float direction : { -1.0f, 1.0f }) {
                    const float offset = std::clamp(
                        bestOffset + direction * step,
                        0.0f,
                        maximumAngle);
                    const float candidateYaw = relationYaw + side * offset;
                    const MATH::Vec3 eye = primary + OrbitDirection(
                        candidateYaw,
                        pitch) * distance;
                    const ScreenCompositionEvaluation evaluation =
                        EvaluateScreenComposition(
                            primary,
                            secondary,
                            eye,
                            lookAt,
                            verticalFovRadians,
                            aspect,
                            targets);
                    const float regularization = maximumAngle > kViewEpsilon
                        ? (offset - preferredAngle) / maximumAngle
                        : 0.0f;
                    const float cost = evaluation.cost +
                        regularization * regularization * 0.08f;
                    if (cost < bestCost) {
                        bestCost = cost;
                        bestOffset = offset;
                        bestYaw = candidateYaw;
                    }
                }
                step *= 0.5f;
            }
            return WrapDegrees(bestYaw);
        }

        float SearchCompositionPitch(
            const CameraFollowComponent& component,
            const MATH::Vec3& primary,
            const MATH::Vec3& secondary,
            const MATH::Vec3& lookAt,
            float yaw,
            float preferredPitch,
            float distance,
            float verticalFovRadians,
            float aspect,
            float framingWeight,
            const ScreenCompositionTargets& targets) noexcept {

            const float range = kCompositionPitchSearchRangeDegrees *
                std::clamp(framingWeight, 0.0f, 1.0f);
            const float minimumPitch = std::clamp(
                preferredPitch - range,
                component.GetMinimumPitchDegrees(),
                component.GetMaximumPitchDegrees());
            const float maximumPitch = std::clamp(
                preferredPitch + range,
                minimumPitch,
                component.GetMaximumPitchDegrees());
            float bestPitch = std::clamp(
                preferredPitch,
                minimumPitch,
                maximumPitch);
            float bestCost = std::numeric_limits<float>::max();
            for (int sample = 0;
                sample < kCompositionPitchSearchSamples;
                ++sample) {
                const float amount = static_cast<float>(sample) /
                    static_cast<float>(kCompositionPitchSearchSamples - 1);
                const float candidatePitch = Lerp(
                    minimumPitch,
                    maximumPitch,
                    amount);
                const MATH::Vec3 eye = primary + OrbitDirection(
                    yaw,
                    candidatePitch) * distance;
                const ScreenCompositionEvaluation evaluation =
                    EvaluateScreenComposition(
                        primary,
                        secondary,
                        eye,
                        lookAt,
                        verticalFovRadians,
                        aspect,
                        targets);
                const float regularization = range > kViewEpsilon
                    ? (candidatePitch - preferredPitch) / range
                    : 0.0f;
                const float cost = evaluation.cost +
                    regularization * regularization * 0.10f;
                if (cost < bestCost) {
                    bestCost = cost;
                    bestPitch = candidatePitch;
                }
            }
            return bestPitch;
        }

        GameObject* ResolveTargetObject(
            World& world,
            GameObject& cameraObject,
            const CameraFollowComponent& component) noexcept {

            if (GameObject* target = world.FindObject(
                    component.GetTargetObjectId())) {
                return target;
            }
            if (!component.GetUseOwnerAsFallbackTarget()) {
                return nullptr;
            }
            return cameraObject.GetParent();
        }

        MATH::Mat4 ResolveTargetWorldMatrix(
            const GameObject& target,
            const PresentationTransformService* presentationTransforms)
            noexcept {
            MATH::Mat4 result =
                target.GetTransform().GetWorldMatrix();
            if (presentationTransforms != nullptr) {
                (void)presentationTransforms->TryGetWorldMatrix(
                    target.GetRuntimeHandle(),
                    result);
            }
            return result;
        }

        float ResolveTargetYawDegrees(
            const MATH::Mat4& targetWorld) noexcept {
            MATH::Vec3 forward = MATH::Normalize(ExtractAxis(
                targetWorld,
                2));
            if (MATH::Length(forward) <= kViewEpsilon) {
                return 0.0f;
            }
            forward.y = 0.0f;
            forward = MATH::Normalize(forward);
            if (MATH::Length(forward) <= kViewEpsilon) {
                return 0.0f;
            }
            // Orbit yaw describes target-to-camera direction, which is the
            // opposite of the target's viewed forward direction.
            return -RadiansToDegrees(std::atan2(forward.x, forward.z));
        }

        bool IsCameraCollisionBlocker(
            const World& world,
            RuntimeObjectHandle objectHandle,
            bool staticOnly) noexcept {

            if (!staticOnly) {
                return true;
            }
            const GameObject* object = world.FindObject(objectHandle);
            const PhysicsBodyComponent* body = object != nullptr
                ? object->GetComponent<PhysicsBodyComponent>()
                : nullptr;
            return body == nullptr || !body->IsEnabled() ||
                body->GetMotionType() == PHYSICS::PhysicsMotionType::Static;
        }

        float ResolveCollisionDistance(
            const World& world,
            const PHYSICS::PhysicsWorldService* physics,
            const CameraFollowComponent& component,
            const GameObject& target,
            const MATH::Vec3& pivot,
            const MATH::Vec3& direction,
            float desiredDistance,
            float minimumDistance,
            bool& outLimited) {

            outLimited = false;
            if (physics == nullptr || !component.IsCollisionEnabled() ||
                desiredDistance <= minimumDistance) {
                return desiredDistance;
            }

            float traveledDistance = 0.0f;
            RuntimeObjectHandle ignoredObject = target.GetRuntimeHandle();
            for (int pass = 0; pass < 4; ++pass) {
                const float remainingDistance =
                    desiredDistance - traveledDistance;
                if (remainingDistance <= kViewEpsilon) {
                    return desiredDistance;
                }

                PHYSICS::PhysicsShapeCastQuery query{};
                query.shape.type = PHYSICS::PhysicsShapeType::Sphere;
                query.shape.radius = component.GetCollisionRadius();
                query.startPose.position =
                    pivot + direction * traveledDistance;
                query.startPose.rotation = MATH::Quat::Identity();
                query.direction = direction;
                query.maxDistance = remainingDistance;
                query.filter.layerMask = component.GetCollisionLayerMask();
                query.filter.includeTriggers = false;
                query.filter.ignoredObject = ignoredObject;

                PHYSICS::PhysicsHit hit{};
                if (!physics->ShapeCast(query, hit)) {
                    return desiredDistance;
                }
                if (!IsCameraCollisionBlocker(
                        world,
                        hit.object,
                        component.GetCollisionStaticOnly())) {
                    const float skipDistance = (std::max)(
                        hit.distance + component.GetCollisionRadius() * 2.0f +
                            component.GetCollisionPadding(),
                        0.05f);
                    traveledDistance += skipDistance;
                    ignoredObject = hit.object;
                    continue;
                }

                const float limitedDistance = std::clamp(
                    traveledDistance + hit.distance -
                        component.GetCollisionPadding(),
                    minimumDistance,
                    desiredDistance);
                outLimited =
                    limitedDistance + kViewEpsilon < desiredDistance;
                return limitedDistance;
            }
            return desiredDistance;
        }

        bool IsCameraPointVisible(
            const World& world,
            const PHYSICS::PhysicsWorldService* physics,
            const CameraFollowComponent& component,
            RuntimeObjectHandle targetHandle,
            const MATH::Vec3& eye,
            const MATH::Vec3& point) {
            if (physics == nullptr) {
                return true;
            }
            MATH::Vec3 delta = point - eye;
            const float distance = MATH::Length(delta);
            if (!MATH::IsFinite(delta) || !std::isfinite(distance) ||
                distance <= kViewEpsilon) {
                return true;
            }
            delta = delta * (1.0f / distance);

            float traveledDistance = 0.0f;
            RuntimeObjectHandle ignoredObject = targetHandle;
            for (int pass = 0; pass < 4; ++pass) {
                const float remainingDistance =
                    distance - traveledDistance;
                if (remainingDistance <= kViewEpsilon) {
                    return true;
                }
                PHYSICS::PhysicsRaycastQuery query{};
                query.origin = eye + delta * traveledDistance;
                query.direction = delta;
                query.maxDistance = remainingDistance;
                query.filter.layerMask = component.GetCollisionLayerMask();
                query.filter.includeTriggers = false;
                query.filter.ignoredObject = ignoredObject;

                PHYSICS::PhysicsHit hit{};
                if (!physics->Raycast(query, hit) ||
                    hit.object == targetHandle) {
                    return true;
                }
                if (IsCameraCollisionBlocker(
                        world,
                        hit.object,
                        component.GetCollisionStaticOnly())) {
                    return false;
                }
                traveledDistance += (std::max)(
                    hit.distance + component.GetCollisionPadding(),
                    0.05f);
                ignoredObject = hit.object;
            }
            return true;
        }

        bool HasPrimarySubjectVisibility(
            const World& world,
            const PHYSICS::PhysicsWorldService* physics,
            const CameraFollowComponent& component,
            RuntimeObjectHandle targetHandle,
            const MATH::Vec3& eye,
            const MATH::Vec3& primaryPivot) {
            const std::array<MATH::Vec3, 3u> samples{
                primaryPivot,
                primaryPivot + MATH::Vec3{
                    0.0f, kPrimaryVisibilityVerticalOffset, 0.0f },
                primaryPivot - MATH::Vec3{
                    0.0f, kPrimaryVisibilityVerticalOffset, 0.0f },
            };
            return std::any_of(
                samples.begin(),
                samples.end(),
                [&](const MATH::Vec3& sample) {
                    return IsCameraPointVisible(
                        world,
                        physics,
                        component,
                        targetHandle,
                        eye,
                        sample);
                });
        }
    }

    namespace CAMERA {

        bool ResolveGameplayCameraSafety(
            const World& world,
            SceneObjectId cameraObjectId,
            Camera3D& camera,
            GameplayCameraSafetyResult* outResult) {
            GameplayCameraSafetyResult result{};
            const auto finish = [&]() {
                if (outResult != nullptr) {
                    *outResult = result;
                }
            };

            const GameObject* cameraObject =
                world.FindObject(cameraObjectId);
            const CameraFollowComponent* component = cameraObject != nullptr
                ? cameraObject->GetComponent<CameraFollowComponent>()
                : nullptr;
            if (component == nullptr || !component->IsEnabled() ||
                !component->HasRuntimeState() ||
                !component->IsCollisionEnabled()) {
                finish();
                return false;
            }

            const GameObject* target = world.FindObject(
                component->GetTargetObjectId());
            if (target == nullptr &&
                component->GetUseOwnerAsFallbackTarget()) {
                target = cameraObject->GetParent();
            }
            const PHYSICS::PhysicsWorldService* physics =
                world.Services().Find<PHYSICS::PhysicsWorldService>();
            if (target == nullptr || physics == nullptr) {
                finish();
                return false;
            }

            const MATH::Vec3 pivot =
                component->GetRuntimeFollowPivot();
            const MATH::Vec3 primaryPivot =
                component->GetRuntimeTargetPivot();
            MATH::Vec3 safeEye = camera.GetPosition();
            MATH::Vec3 safeTarget = camera.GetTarget();
            MATH::Vec3 desiredOffset = safeEye - pivot;
            float desiredDistance = MATH::Length(desiredOffset);
            if (!MATH::IsFinite(pivot) ||
                !MATH::IsFinite(primaryPivot) ||
                !MATH::IsFinite(desiredOffset) ||
                !std::isfinite(desiredDistance) ||
                desiredDistance <= kViewEpsilon) {
                finish();
                return false;
            }

            result.evaluated = true;
            result.desiredDistance = desiredDistance;
            const MATH::Vec3 desiredDirection =
                desiredOffset * (1.0f / desiredDistance);
            bool collisionLimited = false;
            float safeDistance = ResolveCollisionDistance(
                world,
                physics,
                *component,
                *target,
                pivot,
                desiredDirection,
                desiredDistance,
                kFinalCameraEmergencyDistance,
                collisionLimited);
            safeEye = pivot + desiredDirection * safeDistance;
            bool primaryVisible = HasPrimarySubjectVisibility(
                world,
                physics,
                *component,
                target->GetRuntimeHandle(),
                safeEye,
                primaryPivot);

            if (!primaryVisible) {
                const MATH::Vec3 fallbackOffset =
                    component->GetRuntimeEye() - pivot;
                const float fallbackDistance = MATH::Length(fallbackOffset);
                if (MATH::IsFinite(fallbackOffset) &&
                    std::isfinite(fallbackDistance) &&
                    fallbackDistance > kViewEpsilon) {
                    const MATH::Vec3 fallbackDirection =
                        fallbackOffset * (1.0f / fallbackDistance);
                    bool fallbackLimited = false;
                    const float fallbackSafeDistance =
                        ResolveCollisionDistance(
                            world,
                            physics,
                            *component,
                            *target,
                            pivot,
                            fallbackDirection,
                            fallbackDistance,
                            kFinalCameraEmergencyDistance,
                            fallbackLimited);
                    const MATH::Vec3 fallbackEye =
                        pivot + fallbackDirection * fallbackSafeDistance;
                    if (HasPrimarySubjectVisibility(
                            world,
                            physics,
                            *component,
                            target->GetRuntimeHandle(),
                            fallbackEye,
                            primaryPivot)) {
                        safeEye = fallbackEye;
                        safeTarget = component->GetRuntimeLookAt();
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
            const float authoredMinimum = (std::max)(
                component->GetMinimumDistance(),
                kFinalCameraEmergencyDistance);
            result.emergencyContraction = collisionLimited &&
                result.resolvedDistance + kViewEpsilon < authoredMinimum;

            float verticalFov = camera.GetFovYRad();
            if (result.emergencyContraction) {
                const float pressure = std::clamp(
                    1.0f - result.resolvedDistance / authoredMinimum,
                    0.0f,
                    1.0f);
                verticalFov += DegreesToRadians(
                    kFinalCameraEmergencyFovDegrees) * pressure;
            }
            if (!MATH::IsFinite(safeTarget) ||
                MATH::Length(safeTarget - safeEye) <= kViewEpsilon) {
                safeTarget = primaryPivot;
            }
            camera.SetPerspective(
                verticalFov,
                camera.GetAspect(),
                camera.GetNearZ(),
                camera.GetFarZ());
            camera.SetLookAt(safeEye, safeTarget, camera.GetUp());
            finish();
            return true;
        }

    } // namespace CAMERA

    void CameraFollowSystem::OnWorldAttached(World& world) {
        rigService_ = world.Services().Find<CAMERA::CameraRigService>();
        motionIntentService_ = world.Services().Find<
            GAMEPLAY::MotionIntentService>();
        inputService_ = world.Services().Find<INPUT::InputService>();
        physicsService_ = world.Services().Find<
            PHYSICS::PhysicsWorldService>();
        presentationTransforms_ = world.Services().Find<
            PresentationTransformService>();
        runtimePlayState_ = world.Services().Find<
            RuntimePlayStateService>();
        gameplayCameraService_ = world.Services().Find<
            GameplayCameraService>();
    }

    void CameraFollowSystem::OnWorldDetached(World&) {
        rigService_ = nullptr;
        motionIntentService_ = nullptr;
        inputService_ = nullptr;
        physicsService_ = nullptr;
        presentationTransforms_ = nullptr;
        runtimePlayState_ = nullptr;
        gameplayCameraService_ = nullptr;
    }

    void CameraFollowSystem::LateUpdate(
        World& world,
        const FrameContext& frame) {

        if (rigService_ == nullptr) {
            return;
        }
#if defined(HIKARI_WITH_EDITOR)
        if (runtimePlayState_ == nullptr ||
            !runtimePlayState_->IsActive()) {
            return;
        }
#endif

        const float dt = std::isfinite(frame.gameDt)
            ? (std::max)(frame.gameDt, 0.0f)
            : 0.0f;
        // Camera input and target framing are presentation behavior.  Keep
        // them responsive while gameplay is being time-dilated; ordinary
        // world-follow smoothing below intentionally retains gameDt.
        const float cameraDt = std::isfinite(frame.unscaledDt)
            ? std::clamp(frame.unscaledDt, 0.0f, 0.25f)
            : 0.0f;
        const INPUT::InputSnapshot* input = inputService_ != nullptr
            ? &inputService_->GetSnapshot()
            : nullptr;

        world.ForEachObjectWith<CameraComponent, CameraFollowComponent>(
            WorldComponentQueryAnchor::FirstRequiredType,
            [this, input, dt, cameraDt, frameIndex = frame.frameIndex](
                GameObject& cameraObject,
                CameraComponent& cameraComponent,
                CameraFollowComponent& component) {
                if (!component.IsEnabled()) {
                    component.ResetRuntimeState();
                    return;
                }

                GameObject* target = ResolveTargetObject(
                    *cameraObject.GetWorld(),
                    cameraObject,
                    component);
                if (target == nullptr ||
                    cameraObject.GetDocumentId().value == 0u) {
                    component.ResetRuntimeState();
                    return;
                }

                const MATH::Mat4 targetWorld =
                    ResolveTargetWorldMatrix(
                        *target,
                        presentationTransforms_);
                const MATH::Vec3 pivot = TransformPoint(
                    targetWorld,
                    component.GetPivotOffset());
                if (!MATH::IsFinite(pivot)) {
                    component.ResetRuntimeState();
                    return;
                }
                if (!component.HasRuntimeState()) {
                    const MATH::Vec3 direction = OrbitDirection(
                        component.GetInitialYawDegrees(),
                        component.GetInitialPitchDegrees());
                    component.InitializeRuntimeState(
                        pivot + direction * component.GetInitialDistance(),
                        pivot);
                }

                CAMERA::CameraTargetFramingSubmission targetFraming{};
                const bool hasFramingSubmission =
                    rigService_->TryResolveTargetFraming(
                        cameraObject.GetDocumentId(),
                        targetFraming);
                GameObject* secondaryTarget = nullptr;
                MATH::Vec3 requestedSecondaryPivot{};
                if (hasFramingSubmission) {
                    secondaryTarget = cameraObject.GetWorld()->FindObject(
                        targetFraming.targetObject);
                    if (secondaryTarget != nullptr) {
                        const MATH::Mat4 secondaryWorld =
                            ResolveTargetWorldMatrix(
                                *secondaryTarget,
                                presentationTransforms_);
                        requestedSecondaryPivot = TransformPoint(
                            secondaryWorld,
                            targetFraming.targetPivotOffset);
                        if (!MATH::IsFinite(requestedSecondaryPivot)) {
                            secondaryTarget = nullptr;
                        }
                    }
                }

                const bool hadFramingState =
                    component.HasRuntimeFramingState();
                const bool hadFramingSubmission =
                    component.HasRuntimeFramingSubmission();
                const RuntimeObjectHandle previousFramingTarget =
                    component.GetRuntimeFramingTargetObject();
                const RuntimeObjectHandle framingTarget =
                    hasFramingSubmission
                    ? targetFraming.targetObject
                    : previousFramingTarget;
                const CAMERA::CameraRigSourceId framingSourceId =
                    hasFramingSubmission
                    ? targetFraming.sourceId
                    : component.GetRuntimeFramingSourceId();
                const int framingPriority = hasFramingSubmission
                    ? targetFraming.priority
                    : component.GetRuntimeFramingPriority();
                const CAMERA::CameraTargetFramingDynamics framingDynamics =
                    hasFramingSubmission
                    ? targetFraming.dynamics
                    : component.GetRuntimeFramingDynamics();
                const float framingTargetWeight = hasFramingSubmission
                    ? std::clamp(targetFraming.targetWeight, 0.0f, 1.0f)
                    : component.GetRuntimeFramingTargetWeight();
                const float framingDistanceScale = hasFramingSubmission
                    ? (std::max)(targetFraming.distanceScale, 0.0f)
                    : component.GetRuntimeFramingDistanceScale();
                const float framingMaximumDistanceOffset =
                    hasFramingSubmission
                    ? (std::max)(targetFraming.maximumDistanceOffset, 0.0f)
                    : component.GetRuntimeFramingMaximumDistanceOffset();
                const float framingYawSpeed = hasFramingSubmission
                    ? (std::max)(
                        targetFraming.yawTrackingSpeedDegreesPerSecond,
                        0.0f)
                    : component.GetRuntimeFramingYawTrackingSpeed();
                const float framingPreferredPitch = hasFramingSubmission
                    ? targetFraming.preferredPitchDegrees
                    : component.GetRuntimeFramingPreferredPitch();
                const float framingPitchTrackingSpeed = hasFramingSubmission
                    ? (std::max)(
                        targetFraming.pitchTrackingSpeedDegreesPerSecond,
                        0.0f)
                    : component.GetRuntimeFramingPitchTrackingSpeed();
                const bool suppressOrbitInput = hasFramingSubmission
                    ? targetFraming.suppressOrbitInput
                    : component.GetRuntimeFramingSuppressOrbitInput();
                const bool suppressZoomInput = hasFramingSubmission
                    ? targetFraming.suppressZoomInput
                    : component.GetRuntimeFramingSuppressZoomInput();
                const bool relationshipShotSubmission =
                    hasFramingSubmission && targetFraming.relationshipShot;
                const bool hadRelationshipShotState =
                    component.HasRuntimeRelationshipShotState();
                const float relationshipPreferredSide =
                    relationshipShotSubmission
                    ? targetFraming.relationshipPreferredSide
                    : component.GetRuntimeRelationshipPreferredSide();
                const float relationshipShoulderAngleDegrees =
                    relationshipShotSubmission
                    ? targetFraming.relationshipShoulderAngleDegrees
                    : component.GetRuntimeRelationshipShoulderAngleDegrees();
                const float relationshipVerticalFovOffsetRadians =
                    relationshipShotSubmission
                    ? targetFraming.relationshipVerticalFovOffsetRadians
                    : component.
                        GetRuntimeRelationshipVerticalFovOffsetRadians();
                const float relationshipRollRadians =
                    relationshipShotSubmission
                    ? targetFraming.relationshipRollRadians
                    : component.GetRuntimeRelationshipRollRadians();
                const float relationshipSideClearanceBias =
                    relationshipShotSubmission
                    ? targetFraming.relationshipSideClearanceBias
                    : component.GetRuntimeRelationshipSideClearanceBias();
                const float relationshipDistanceOffset =
                    relationshipShotSubmission
                    ? targetFraming.relationshipDistanceOffset
                    : component.GetRuntimeRelationshipDistanceOffset();
                const float relationshipPivotHeightOffset =
                    relationshipShotSubmission
                    ? targetFraming.relationshipPivotHeightOffset
                    : component.GetRuntimeRelationshipPivotHeightOffset();
                const float relationshipFocusLateralOffset =
                    relationshipShotSubmission
                    ? targetFraming.relationshipFocusLateralOffset
                    : component.GetRuntimeRelationshipFocusLateralOffset();
                const float relationshipFocusHeightOffset =
                    relationshipShotSubmission
                    ? targetFraming.relationshipFocusHeightOffset
                    : component.GetRuntimeRelationshipFocusHeightOffset();
                const bool relationshipSuppressModifiers =
                    relationshipShotSubmission
                    ? targetFraming.relationshipSuppressModifiers
                    : component.GetRuntimeRelationshipSuppressModifiers();
                float relationshipYawOffsetDegrees =
                    hadRelationshipShotState
                    ? component.GetRuntimeRelationshipYawOffsetDegrees()
                    : 0.0f;
                const bool targetHandleChanged = hasFramingSubmission &&
                    (!hadFramingSubmission ||
                        framingTarget != previousFramingTarget ||
                        framingSourceId !=
                            component.GetRuntimeFramingSourceId() ||
                        framingPriority !=
                            component.GetRuntimeFramingPriority());
                const float previousFramingWeight = hadFramingState
                    ? component.GetRuntimeFramingWeight()
                    : 0.0f;
                const bool framingStateDormant = !hadFramingState ||
                    (previousFramingWeight <= kInputEpsilon &&
                        std::abs(component.GetRuntimeFramingWeightVelocity()) <=
                            kViewEpsilon &&
                        MATH::Length(
                            component.GetRuntimeFramingFocusOffset()) <=
                            kViewEpsilon &&
                        MATH::Length(
                            component.GetRuntimeFramingFocusVelocity()) <=
                            kViewEpsilon &&
                        MATH::Length(
                            component.GetRuntimeFramingSecondaryPivotVelocity()) <=
                            kViewEpsilon &&
                        std::abs(component.GetRuntimeFramingYawVelocity()) <=
                            kViewEpsilon &&
                        std::abs(component.GetRuntimeFramingPitchVelocity()) <=
                            kViewEpsilon &&
                        std::abs(component.GetRuntimeFramingDistanceVelocity()) <=
                            kViewEpsilon);
                bool framingSwitchActive = hadFramingState &&
                    component.GetRuntimeFramingSwitchActive();
                float framingSwitchRemainingSeconds = hadFramingState
                    ? (std::max)(
                        component.GetRuntimeFramingSwitchRemainingSeconds(),
                        0.0f)
                    : 0.0f;
                if (framingStateDormant) {
                    framingSwitchActive = false;
                    framingSwitchRemainingSeconds = 0.0f;
                } else if (targetHandleChanged) {
                    const float switchSmoothSeconds = (std::max)(
                        framingDynamics.targetSwitchSmoothSeconds,
                        0.0f);
                    // Let the critically damped switch spring run for a few
                    // time constants, then return stable targets to the
                    // single focus spring even when the target keeps moving.
                    framingSwitchRemainingSeconds = std::isfinite(
                        switchSmoothSeconds)
                        ? std::min(switchSmoothSeconds, 10.0f) * 3.0f
                        : 0.0f;
                    framingSwitchActive =
                        framingSwitchRemainingSeconds > kInputEpsilon;
                }

                const MATH::Vec3 previousSecondaryPivot =
                    component.GetRuntimeFramingSecondaryPivot();
                MATH::Vec3 secondaryPivot = previousSecondaryPivot;
                MATH::Vec3 secondaryPivotVelocity = component
                    .GetRuntimeFramingSecondaryPivotVelocity();
                if (framingStateDormant) {
                    secondaryPivot = secondaryTarget != nullptr
                        ? requestedSecondaryPivot
                        : pivot;
                    secondaryPivotVelocity = {};
                } else if (secondaryTarget != nullptr &&
                    framingSwitchActive) {
                    secondaryPivot = SmoothDampVector(
                        secondaryPivot,
                        requestedSecondaryPivot,
                        secondaryPivotVelocity,
                        framingDynamics.targetSwitchSmoothSeconds,
                        cameraDt);
                    if (MATH::Length(
                            requestedSecondaryPivot - secondaryPivot) <=
                            kViewEpsilon &&
                        MATH::Length(secondaryPivotVelocity) <=
                            kViewEpsilon) {
                        framingSwitchActive = false;
                    }
                } else if (secondaryTarget != nullptr) {
                    // A stable target's presentation pivot is fed directly to
                    // the explicit focus spring below.  Only a real handle or
                    // source switch needs a secondary-pivot spring.
                    secondaryPivot = requestedSecondaryPivot;
                    secondaryPivotVelocity = {};
                } else {
                    // Keep the last valid pivot while the target is absent;
                    // the framing weight and focus then fade out smoothly.
                    secondaryPivot = SmoothDampVector(
                        secondaryPivot,
                        secondaryPivot,
                        secondaryPivotVelocity,
                        framingDynamics.exitSmoothSeconds,
                        cameraDt);
                    if (MATH::Length(secondaryPivotVelocity) <=
                        kViewEpsilon) {
                        framingSwitchActive = false;
                    }
                }
                if (framingSwitchActive) {
                    framingSwitchRemainingSeconds = (std::max)(
                        framingSwitchRemainingSeconds - cameraDt,
                        0.0f);
                    if (framingSwitchRemainingSeconds <= kInputEpsilon) {
                        framingSwitchActive = false;
                    }
                } else {
                    framingSwitchRemainingSeconds = 0.0f;
                }

                const float currentFramingWeight = previousFramingWeight;
                float framingWeightVelocity = hadFramingState
                    ? component.GetRuntimeFramingWeightVelocity()
                    : 0.0f;
                const float desiredFramingWeight =
                    secondaryTarget != nullptr && hasFramingSubmission
                    ? std::clamp(targetFraming.weight, 0.0f, 1.0f)
                    : 0.0f;
                const float framingWeightSmoothSeconds =
                    desiredFramingWeight > currentFramingWeight + kInputEpsilon
                    ? framingDynamics.enterSmoothSeconds
                    : framingDynamics.exitSmoothSeconds;
                const float framingWeight = SmoothDampScalar(
                    currentFramingWeight,
                    desiredFramingWeight,
                    framingWeightVelocity,
                    framingWeightSmoothSeconds,
                    -1.0f,
                    cameraDt);
                const float clampedFramingWeight = std::clamp(
                    framingWeight,
                    0.0f,
                    1.0f);
                const float previousRelationshipWeight =
                    hadRelationshipShotState
                    ? component.GetRuntimeRelationshipWeight()
                    : 0.0f;
                float relationshipWeightVelocity =
                    hadRelationshipShotState
                    ? component.GetRuntimeRelationshipWeightVelocity()
                    : 0.0f;
                const float desiredRelationshipWeight =
                    relationshipShotSubmission && secondaryTarget != nullptr
                    ? std::clamp(targetFraming.weight, 0.0f, 1.0f)
                    : 0.0f;
                const float relationshipSmoothSeconds =
                    desiredRelationshipWeight >
                            previousRelationshipWeight + kInputEpsilon
                    ? framingDynamics.enterSmoothSeconds
                    : framingDynamics.exitSmoothSeconds;
                const float relationshipWeight = SmoothDampScalar(
                    previousRelationshipWeight,
                    desiredRelationshipWeight,
                    relationshipWeightVelocity,
                    relationshipSmoothSeconds,
                    -1.0f,
                    cameraDt);
                const float clampedRelationshipWeight = std::clamp(
                    relationshipWeight,
                    0.0f,
                    1.0f);
                const bool relationshipShotResponseActive =
                    desiredRelationshipWeight > kInputEpsilon ||
                    clampedRelationshipWeight > kInputEpsilon;
                const bool hasTargetFraming =
                    secondaryTarget != nullptr &&
                    hasFramingSubmission &&
                    desiredFramingWeight > kInputEpsilon;
                const bool framingResponseActive =
                    desiredFramingWeight > kInputEpsilon ||
                    clampedFramingWeight > kInputEpsilon ||
                    MATH::Length(component.GetRuntimeFramingFocusOffset()) >
                        kViewEpsilon;

                float yaw = component.GetRuntimeYawDegrees();
                float pitch = component.GetRuntimePitchDegrees();
                float desiredDistance =
                    component.GetRuntimeDesiredDistance();
                float lookIdleSeconds =
                    component.GetRuntimeLookIdleSeconds();

                std::array<float, 2> look{};
                float zoom = 0.0f;
                bool recenterPressed = false;
                if (input != nullptr && component.IsOrbitInputEnabled()) {
                    const float inputRestore = 1.0f - clampedFramingWeight;
                    if (!component.GetLookActionId().empty()) {
                        look = input->GetAxis2D(
                            component.GetLookActionId());
                        if (suppressOrbitInput) {
                            look[0] *= inputRestore;
                            look[1] *= inputRestore;
                        }
                    }
                    if (!component.GetZoomActionId().empty()) {
                        zoom = input->GetAxis1D(
                            component.GetZoomActionId());
                        if (suppressZoomInput) {
                            zoom *= inputRestore;
                        }
                    }
                    if (!std::isfinite(look[0])) {
                        look[0] = 0.0f;
                    }
                    if (!std::isfinite(look[1])) {
                        look[1] = 0.0f;
                    }
                    if (!std::isfinite(zoom)) {
                        zoom = 0.0f;
                    }
                    if (!component.GetRecenterActionId().empty() &&
                        inputRestore > kInputEpsilon) {
                        recenterPressed = input->IsPressed(
                            component.GetRecenterActionId());
                    }
                }

                const bool hasLookInput =
                    std::abs(look[0]) > kInputEpsilon ||
                    std::abs(look[1]) > kInputEpsilon;
                if (hasLookInput) {
                    const bool gamepadLook = input != nullptr &&
                        input->GetLastActiveDevice() ==
                            INPUT::InputDeviceKind::Gamepad;
                    const float yawAmount = gamepadLook
                        ? component.GetYawSpeedDegreesPerSecond() * cameraDt
                        : component.GetMouseSensitivityDegreesPerPixel();
                    const float pitchAmount = gamepadLook
                        ? component.GetPitchSpeedDegreesPerSecond() * cameraDt
                        : component.GetMouseSensitivityDegreesPerPixel();
                    // Orbit direction points from the target to the camera,
                    // so it turns opposite to the viewed direction.
                    yaw -= look[0] * yawAmount;
                    const float verticalSign =
                        component.GetInvertVerticalLook() ? -1.0f : 1.0f;
                    pitch += look[1] * verticalSign * pitchAmount;
                    lookIdleSeconds = 0.0f;
                } else {
                    lookIdleSeconds += cameraDt;
                }

                if (std::abs(zoom) > kInputEpsilon) {
                    const bool gamepadZoom = input != nullptr &&
                        input->GetLastActiveDevice() ==
                            INPUT::InputDeviceKind::Gamepad;
                    const float zoomAmount = gamepadZoom
                        ? component.GetZoomSpeedUnitsPerSecond() * cameraDt
                        : component.GetMouseWheelZoomUnitsPerStep();
                    desiredDistance -= zoom * zoomAmount;
                }
                desiredDistance = std::clamp(
                    desiredDistance,
                    component.GetMinimumDistance(),
                    component.GetMaximumDistance());

                const float followAmount = SmoothFactor(
                    component.GetFollowSmooth(), dt);
                const MATH::Vec3 trackedFollowPivot =
                    component.GetRuntimeFollowPivot() +
                    (pivot - component.GetRuntimeFollowPivot()) *
                        followAmount;
                MATH::Vec3 followPivot = trackedFollowPivot;
                if (relationshipShotResponseActive) {
                    followPivot.y += relationshipPivotHeightOffset *
                        clampedRelationshipWeight;
                }
                float compositionCollisionBlend = hadFramingState
                    ? component.GetRuntimeScreenCompositionCollisionBlend()
                    : 0.0f;
                float compositionCollisionBlendVelocity = hadFramingState
                    ? component
                        .GetRuntimeScreenCompositionCollisionBlendVelocity()
                    : 0.0f;
                float compositionSide = hadFramingState
                    ? component.GetRuntimeScreenCompositionSide()
                    : 1.0f;
                const float screenCompositionWeight =
                    clampedFramingWeight * ScreenCompositionPolicyWeight(
                        framingTargetWeight);
                const bool screenCompositionActive =
                    component.IsScreenCompositionEnabled() &&
                    secondaryTarget != nullptr &&
                    framingResponseActive &&
                    screenCompositionWeight > kInputEpsilon;

                MATH::Vec3 focusVelocity = hadFramingState
                    ? component.GetRuntimeFramingFocusVelocity()
                    : MATH::Vec3{};
                MATH::Vec3 focusOffset = hadFramingState
                    ? component.GetRuntimeFramingFocusOffset()
                    : MATH::Vec3{};
                const float focusWeight =
                    std::clamp(framingTargetWeight, 0.0f, 1.0f) *
                    clampedFramingWeight;
                MATH::Vec3 desiredFocusOffset =
                    (secondaryPivot - followPivot) * focusWeight;
                if (relationshipShotResponseActive) {
                    MATH::Vec3 relationshipForward =
                        secondaryPivot - followPivot;
                    relationshipForward.y = 0.0f;
                    if (MATH::IsFinite(relationshipForward) &&
                        MATH::Length(relationshipForward) > kViewEpsilon) {
                        relationshipForward = MATH::Normalize(
                            relationshipForward);
                        const MATH::Vec3 relationshipRight{
                            relationshipForward.z,
                            0.0f,
                            -relationshipForward.x };
                        const float relationshipSide =
                            std::abs(relationshipYawOffsetDegrees) > 0.01f
                            ? relationshipYawOffsetDegrees >= 0.0f
                                ? 1.0f
                                : -1.0f
                            : 0.0f;
                        desiredFocusOffset = desiredFocusOffset +
                            relationshipRight *
                                (relationshipFocusLateralOffset *
                                    relationshipSide *
                                    clampedRelationshipWeight);
                    }
                    desiredFocusOffset.y +=
                        relationshipFocusHeightOffset *
                            clampedRelationshipWeight;
                }
                if (screenCompositionActive) {
                    const float collisionHeightReduction = 1.0f -
                        std::clamp(
                            compositionCollisionBlend *
                                component
                                    .GetScreenCompositionCollisionCenteringStrength() *
                                0.50f,
                            0.0f,
                            0.85f);
                    desiredFocusOffset.y +=
                        component.GetScreenCompositionFocusHeightBias() *
                        screenCompositionWeight * collisionHeightReduction;
                }
                focusOffset = SmoothDampVector(
                    focusOffset,
                    desiredFocusOffset,
                    focusVelocity,
                    framingDynamics.focusSmoothSeconds,
                    cameraDt);
                const bool focusResponseActive = framingResponseActive ||
                    MATH::Length(focusOffset) > kViewEpsilon ||
                    MATH::Length(focusVelocity) > kViewEpsilon;
                const MATH::Vec3 lookAt = focusResponseActive
                    ? followPivot + focusOffset
                    : component.GetRuntimeLookAt() +
                        (pivot - component.GetRuntimeLookAt()) *
                        SmoothFactor(component.GetLookSmooth(), dt);

                float framingDistance = desiredDistance;
                if (secondaryTarget != nullptr) {
                    MATH::Vec3 separation = secondaryPivot - pivot;
                    separation.y = 0.0f;
                    const float separationLength = MATH::Length(separation);
                    const float scaledSeparation =
                        std::isfinite(separationLength)
                        ? separationLength * framingDistanceScale
                        : framingMaximumDistanceOffset;
                    const float distanceOffset = std::min(
                        framingMaximumDistanceOffset,
                        std::isfinite(scaledSeparation)
                        ? scaledSeparation
                        : framingMaximumDistanceOffset);
                    framingDistance = std::clamp(
                        desiredDistance +
                            distanceOffset * clampedFramingWeight,
                        component.GetMinimumDistance(),
                        component.GetMaximumDistance());
                }
                if (relationshipShotResponseActive) {
                    framingDistance = std::clamp(
                        framingDistance + relationshipDistanceOffset *
                            clampedRelationshipWeight,
                        component.GetMinimumDistance(),
                        component.GetMaximumDistance());
                }

                const float compositionDistance = std::clamp(
                    std::isfinite(component.GetRuntimeDistance())
                        ? component.GetRuntimeDistance()
                        : framingDistance,
                    component.GetMinimumDistance(),
                    component.GetMaximumDistance());
                // An active relationship shot owns the pose FOV. Starting
                // from the authored component FOV prevents the previous
                // frame's relationship offset and ordinary modifiers from
                // being accumulated again on every LateUpdate.
                float verticalFovRadians = relationshipShotResponseActive
                    ? cameraComponent.GetFovYRad()
                    : ResolveGameplayVerticalFov(
                        gameplayCameraService_,
                        cameraComponent.GetFovYRad());
                if (relationshipShotResponseActive) {
                    verticalFovRadians = std::clamp(
                        verticalFovRadians +
                            relationshipVerticalFovOffsetRadians *
                                clampedRelationshipWeight,
                        DegreesToRadians(5.0f),
                        DegreesToRadians(150.0f));
                }
                const float gameplayAspect = ResolveGameplayAspect(
                    gameplayCameraService_);
                MATH::Vec3 compositionSecondaryPivot = secondaryPivot;
                if (screenCompositionActive && hadFramingSubmission &&
                    !targetHandleChanged && !framingSwitchActive &&
                    cameraDt > kViewEpsilon) {
                    MATH::Vec3 prediction =
                        (secondaryPivot - previousSecondaryPivot) *
                        (kCompositionTargetPredictionSeconds / cameraDt);
                    const float predictionLength = MATH::Length(prediction);
                    if (MATH::IsFinite(prediction) &&
                        std::isfinite(predictionLength) &&
                        predictionLength > kCompositionMaximumPredictionDistance) {
                        prediction = prediction *
                            (kCompositionMaximumPredictionDistance /
                                predictionLength);
                    }
                    if (MATH::IsFinite(prediction)) {
                        compositionSecondaryPivot =
                            compositionSecondaryPivot + prediction;
                    }
                }
                MATH::Vec3 targetDirection = secondaryPivot - pivot;
                targetDirection.y = 0.0f;
                const bool hasRelationDirection =
                    MATH::IsFinite(targetDirection) &&
                    MATH::Length(targetDirection) > kViewEpsilon;
                float relationYaw = yaw;
                if (hasRelationDirection) {
                    targetDirection = MATH::Normalize(targetDirection);
                    relationYaw = -RadiansToDegrees(std::atan2(
                        targetDirection.x,
                        targetDirection.z));
                }
                float compositionRelationYaw = relationYaw;
                MATH::Vec3 compositionTargetDirection =
                    compositionSecondaryPivot - pivot;
                compositionTargetDirection.y = 0.0f;
                if (screenCompositionActive &&
                    MATH::IsFinite(compositionTargetDirection) &&
                    MATH::Length(compositionTargetDirection) > kViewEpsilon) {
                    compositionTargetDirection = MATH::Normalize(
                        compositionTargetDirection);
                    compositionRelationYaw = -RadiansToDegrees(std::atan2(
                        compositionTargetDirection.x,
                        compositionTargetDirection.z));
                }

                const bool chooseRelationshipSide =
                    relationshipShotSubmission && hasRelationDirection &&
                    (!hadRelationshipShotState || targetHandleChanged ||
                        !component.HasRuntimeRelationshipShotSubmission());
                if (chooseRelationshipSide) {
                    const float shoulderAngle = std::clamp(
                        relationshipShoulderAngleDegrees,
                        0.0f,
                        75.0f);
                    const float preferredSign =
                        std::abs(relationshipPreferredSide) > 0.10f
                        ? relationshipPreferredSide >= 0.0f ? 1.0f : -1.0f
                        : compositionSide >= 0.0f ? 1.0f : -1.0f;
                    const float preferredOffset =
                        preferredSign * shoulderAngle;
                    const float alternateOffset = -preferredOffset;
                    const auto clearanceForOffset = [&](float yawOffset) {
                        bool limited = false;
                        return ResolveCollisionDistance(
                            *cameraObject.GetWorld(),
                            physicsService_,
                            component,
                            *target,
                            followPivot,
                            OrbitDirection(
                                WrapDegrees(relationYaw + yawOffset),
                                framingPreferredPitch),
                            framingDistance,
                            component.GetMinimumDistance(),
                            limited);
                    };
                    const float preferredClearance =
                        clearanceForOffset(preferredOffset);
                    const float alternateClearance =
                        clearanceForOffset(alternateOffset);
                    const float centeredClearance =
                        clearanceForOffset(0.0f);
                    const float bestOtherClearance = (std::max)(
                        alternateClearance,
                        centeredClearance);
                    if (preferredClearance +
                            relationshipSideClearanceBias >=
                        bestOtherClearance) {
                        relationshipYawOffsetDegrees = preferredOffset;
                    }
                    else if (alternateClearance >= centeredClearance) {
                        relationshipYawOffsetDegrees = alternateOffset;
                    }
                    else {
                        relationshipYawOffsetDegrees = 0.0f;
                    }
                    if (std::abs(relationshipYawOffsetDegrees) > 0.01f) {
                        compositionSide = relationshipYawOffsetDegrees >= 0.0f
                            ? 1.0f
                            : -1.0f;
                    }
                    else {
                        compositionSide = preferredSign;
                    }
                }
                else if (relationshipShotSubmission &&
                    hasRelationDirection &&
                    std::abs(relationshipYawOffsetDegrees) > 0.01f) {
                    // Keep the collision-selected side fixed, but allow the
                    // authored shoulder magnitude to evolve with the shot.
                    relationshipYawOffsetDegrees =
                        (relationshipYawOffsetDegrees >= 0.0f
                            ? 1.0f
                            : -1.0f) *
                        std::clamp(
                            relationshipShoulderAngleDegrees,
                            0.0f,
                            75.0f);
                }

                if (!relationshipShotResponseActive &&
                    screenCompositionActive && hasRelationDirection &&
                    (framingStateDormant || targetHandleChanged ||
                        !component.IsRuntimeScreenCompositionActive())) {
                    const MATH::Vec3 currentEye = followPivot +
                        OrbitDirection(yaw, pitch) * compositionDistance;
                    const ViewportProjection primaryProjection =
                        ProjectPointToViewport(
                            currentEye,
                            lookAt,
                            followPivot,
                            verticalFovRadians,
                            gameplayAspect);
                    const ViewportProjection secondaryProjection =
                        ProjectPointToViewport(
                            currentEye,
                            lookAt,
                            secondaryPivot,
                            verticalFovRadians,
                            gameplayAspect);
                    if (primaryProjection.valid && secondaryProjection.valid &&
                        std::abs(
                            secondaryProjection.x - primaryProjection.x) >
                            0.015f) {
                        compositionSide = secondaryProjection.x >=
                            primaryProjection.x
                            ? 1.0f
                            : -1.0f;
                    } else {
                        const float currentShoulder = WrapDegrees(
                            yaw - relationYaw);
                        if (std::abs(currentShoulder) > 3.0f) {
                            compositionSide = currentShoulder >= 0.0f
                                ? 1.0f
                                : -1.0f;
                        }
                    }
                }

                const ScreenCompositionTargets compositionTargets =
                    BuildCompositionTargets(
                        component,
                        compositionSide,
                        screenCompositionWeight,
                        compositionCollisionBlend);
                const MATH::Vec3 compositionEye = followPivot +
                    OrbitDirection(yaw, pitch) * compositionDistance;
                const ScreenCompositionEvaluation currentComposition =
                    screenCompositionActive
                    ? EvaluateScreenComposition(
                        followPivot,
                        compositionSecondaryPivot,
                        compositionEye,
                        lookAt,
                        verticalFovRadians,
                        gameplayAspect,
                        compositionTargets)
                    : ScreenCompositionEvaluation{};

                float framingYawVelocity = hadFramingState
                    ? component.GetRuntimeFramingYawVelocity()
                    : 0.0f;
                float framingPitchVelocity = hadFramingState
                    ? component.GetRuntimeFramingPitchVelocity()
                    : 0.0f;
                if (framingResponseActive) {
                    if (framingYawSpeed <= kInputEpsilon) {
                        framingYawVelocity = 0.0f;
                    } else if (hasRelationDirection) {
                        float targetYaw = relationYaw;
                        float yawResponseWeight = clampedFramingWeight;
                        float yawSmoothSeconds =
                            framingDynamics.yawSmoothSeconds;
                        if (relationshipShotResponseActive) {
                            targetYaw = WrapDegrees(
                                relationYaw +
                                    relationshipYawOffsetDegrees);
                            yawResponseWeight = clampedRelationshipWeight;
                        }
                        else if (screenCompositionActive) {
                            targetYaw = currentComposition
                                .horizontalSoftViolation
                                ? SearchCompositionYaw(
                                    component,
                                    followPivot,
                                    compositionSecondaryPivot,
                                    lookAt,
                                    compositionRelationYaw,
                                    compositionSide,
                                    pitch,
                                    compositionDistance,
                                    verticalFovRadians,
                                    gameplayAspect,
                                    screenCompositionWeight,
                                    compositionCollisionBlend,
                                    compositionTargets)
                                : yaw;
                            if (currentComposition
                                .horizontalHardViolation) {
                                yawSmoothSeconds *= component
                                    .GetScreenCompositionHardResponseScale();
                            }
                        }
                        const float yawError = WrapDegrees(targetYaw - yaw);
                        const float deadZone = std::clamp(
                            framingDynamics.yawDeadZoneDegrees,
                            0.0f,
                            180.0f);
                        const float outsideDeadZone = (std::max)(
                            std::abs(yawError) - deadZone,
                            0.0f);
                        const float yawTarget = yaw +
                            (yawError >= 0.0f ? 1.0f : -1.0f) *
                            outsideDeadZone * yawResponseWeight;
                        yaw = SmoothDampAngle(
                            yaw,
                            yawTarget,
                            framingYawVelocity,
                            yawSmoothSeconds,
                            (std::max)(
                                framingYawSpeed,
                                0.0f) * yawResponseWeight,
                            cameraDt);
                    } else {
                        framingYawVelocity = 0.0f;
                    }

                    const float preferredPitch = std::clamp(
                        framingPreferredPitch,
                        component.GetMinimumPitchDegrees(),
                        component.GetMaximumPitchDegrees());
                    float resolvedPitchTarget = preferredPitch;
                    float pitchSmoothSeconds =
                        framingDynamics.pitchSmoothSeconds;
                    if (screenCompositionActive) {
                        resolvedPitchTarget = currentComposition
                            .verticalSoftViolation
                            ? SearchCompositionPitch(
                                component,
                                followPivot,
                                compositionSecondaryPivot,
                                lookAt,
                                yaw,
                                preferredPitch,
                                compositionDistance,
                                verticalFovRadians,
                                gameplayAspect,
                                screenCompositionWeight,
                                compositionTargets)
                            : pitch;
                        if (currentComposition.verticalHardViolation) {
                            pitchSmoothSeconds *= component
                                .GetScreenCompositionHardResponseScale();
                        }
                    }
                    const float pitchTarget = pitch +
                        (resolvedPitchTarget - pitch) *
                            clampedFramingWeight;
                    if (framingPitchTrackingSpeed <= kInputEpsilon) {
                        framingPitchVelocity = 0.0f;
                    } else {
                        pitch = SmoothDampScalar(
                            pitch,
                            pitchTarget,
                            framingPitchVelocity,
                            pitchSmoothSeconds,
                            framingPitchTrackingSpeed *
                                clampedFramingWeight,
                            cameraDt);
                    }
                    lookIdleSeconds = 0.0f;
                } else {
                    framingYawVelocity = 0.0f;
                    framingPitchVelocity = 0.0f;
                }

                const float targetSpeed = dt > kViewEpsilon
                    ? MATH::Length(
                        pivot - component.GetRuntimeTargetPivot()) / dt
                    : 0.0f;
                GAMEPLAY::MotionIntent motionIntent{};
                const bool movingBackward =
                    motionIntentService_ != nullptr &&
                    motionIntentService_->PeekIntent(
                        target->GetRuntimeHandle(),
                        frameIndex,
                        motionIntent) &&
                    motionIntent.move.y < kBackwardMovementThreshold;
                const bool automaticRecenter =
                    !hasTargetFraming &&
                    clampedFramingWeight <= kInputEpsilon &&
                    component.IsAutoRecenterEnabled() &&
                    !movingBackward &&
                    targetSpeed > 0.05f &&
                    lookIdleSeconds >=
                        component.GetAutoRecenterDelaySeconds();
                if (recenterPressed || automaticRecenter) {
                    const float recenterDelta = recenterPressed
                        ? 360.0f
                        : component.GetAutoRecenterSpeedDegreesPerSecond() *
                            cameraDt;
                    yaw = MoveTowardsAngle(
                        yaw,
                        ResolveTargetYawDegrees(targetWorld),
                        recenterDelta);
                }
                yaw = WrapDegrees(yaw);
                pitch = std::clamp(
                    pitch,
                    component.GetMinimumPitchDegrees(),
                    component.GetMaximumPitchDegrees());
                const MATH::Vec3 orbitDirection = OrbitDirection(yaw, pitch);

                bool collisionLimited = false;
                const float collisionDistance = ResolveCollisionDistance(
                    *cameraObject.GetWorld(),
                    physicsService_,
                    component,
                    *target,
                    followPivot,
                    orbitDirection,
                    framingDistance,
                    component.GetMinimumDistance(),
                    collisionLimited);
                float resolvedDistance = component.GetRuntimeDistance();
                float distanceVelocity = hadFramingState
                    ? component.GetRuntimeFramingDistanceVelocity()
                    : 0.0f;
                if (!std::isfinite(resolvedDistance)) {
                    resolvedDistance = framingDistance;
                    distanceVelocity = 0.0f;
                }
                const bool collisionPushIn = collisionLimited &&
                    collisionDistance < resolvedDistance;
                if (collisionPushIn) {
                    resolvedDistance = collisionDistance;
                    distanceVelocity = 0.0f;
                } else if (framingResponseActive ||
                    (component.WasCollisionLimited() &&
                        !collisionLimited)) {
                    const bool releasingCollision =
                        component.WasCollisionLimited() &&
                        !collisionLimited;
                    const bool expanding =
                        collisionDistance > resolvedDistance;
                    const float smoothSeconds = releasingCollision
                        ? framingDynamics.distanceContractSmoothSeconds
                        : (expanding
                            ? framingDynamics.distanceExpandSmoothSeconds
                            : framingDynamics.distanceContractSmoothSeconds);
                    resolvedDistance = SmoothDampScalar(
                        resolvedDistance,
                        collisionDistance,
                        distanceVelocity,
                        smoothSeconds,
                        -1.0f,
                        cameraDt);
                } else {
                    resolvedDistance +=
                        (collisionDistance - resolvedDistance) *
                        SmoothFactor(component.GetDistanceSmooth(), dt);
                    // The ordinary world-follow path owns distance directly;
                    // do not retain a stale framing spring velocity here.
                    distanceVelocity = 0.0f;
                }

                const MATH::Vec3 eye =
                    followPivot + orbitDirection * resolvedDistance;

                const float collisionRange = (std::max)(
                    framingDistance - component.GetMinimumDistance(),
                    0.01f);
                const float collisionPressure = collisionLimited
                    ? std::clamp(
                        (framingDistance - resolvedDistance) /
                            collisionRange * 1.4f,
                        0.0f,
                        1.0f)
                    : 0.0f;
                const float collisionBlendSmoothSeconds =
                    collisionPressure > compositionCollisionBlend
                    ? 0.10f
                    : 0.28f;
                compositionCollisionBlend = SmoothDampScalar(
                    compositionCollisionBlend,
                    screenCompositionActive ? collisionPressure : 0.0f,
                    compositionCollisionBlendVelocity,
                    collisionBlendSmoothSeconds,
                    -1.0f,
                    cameraDt);
                const ScreenCompositionTargets finalCompositionTargets =
                    BuildCompositionTargets(
                        component,
                        compositionSide,
                        screenCompositionWeight,
                        compositionCollisionBlend);
                const ScreenCompositionEvaluation finalComposition =
                    screenCompositionActive
                    ? EvaluateScreenComposition(
                        followPivot,
                        secondaryPivot,
                        eye,
                        lookAt,
                        verticalFovRadians,
                        gameplayAspect,
                        finalCompositionTargets)
                    : ScreenCompositionEvaluation{};

                component.SetRuntimeOrbit(
                    yaw,
                    pitch,
                    desiredDistance,
                    resolvedDistance,
                    lookIdleSeconds);
                component.SetRuntimeView(
                    eye,
                    lookAt,
                    collisionLimited);
                component.SetRuntimeTracking(trackedFollowPivot, pivot);
                component.SetRuntimeFramingState(
                    hasFramingSubmission,
                    framingTarget,
                    framingSourceId,
                    framingPriority,
                    framingDynamics,
                    framingTargetWeight,
                    framingDistanceScale,
                    framingMaximumDistanceOffset,
                    framingYawSpeed,
                    framingPreferredPitch,
                    framingPitchTrackingSpeed,
                    suppressOrbitInput,
                    suppressZoomInput,
                    framingSwitchActive,
                    framingSwitchRemainingSeconds,
                    secondaryPivot,
                    clampedFramingWeight,
                    framingWeightVelocity,
                    secondaryPivotVelocity,
                    framingYawVelocity,
                    framingPitchVelocity,
                    focusOffset,
                    focusVelocity,
                    distanceVelocity);
                component.SetRuntimeRelationshipShotState(
                    relationshipShotSubmission,
                    relationshipPreferredSide,
                    relationshipShoulderAngleDegrees,
                    relationshipVerticalFovOffsetRadians,
                    relationshipRollRadians,
                    relationshipSideClearanceBias,
                    relationshipDistanceOffset,
                    relationshipPivotHeightOffset,
                    relationshipFocusLateralOffset,
                    relationshipFocusHeightOffset,
                    relationshipSuppressModifiers,
                    relationshipYawOffsetDegrees,
                    clampedRelationshipWeight,
                    relationshipWeightVelocity);
                component.SetRuntimeScreenCompositionState(
                    screenCompositionActive,
                    compositionSide,
                    screenCompositionActive
                        ? finalComposition.primary.x
                        : 0.5f,
                    screenCompositionActive
                        ? finalComposition.primary.y
                        : 0.5f,
                    screenCompositionActive
                        ? finalComposition.secondary.x
                        : 0.5f,
                    screenCompositionActive
                        ? finalComposition.secondary.y
                        : 0.5f,
                    screenCompositionActive &&
                        finalComposition.HasSoftViolation(),
                    screenCompositionActive &&
                        finalComposition.HasHardViolation(),
                    compositionCollisionBlend,
                    compositionCollisionBlendVelocity);

                CAMERA::CameraRigSubmission submission{};
                submission.cameraObjectId =
                    cameraObject.GetDocumentId();
                submission.sourceId = kFollowRigSource;
                submission.priority = component.GetPriority();
                submission.pose.eye = eye;
                submission.pose.target = lookAt;
                MATH::Vec3 resolvedUp{ 0.0f, 1.0f, 0.0f };
                if (relationshipShotResponseActive &&
                    std::abs(relationshipRollRadians) > kViewEpsilon) {
                    const MATH::Vec3 viewForward = MATH::Normalize(
                        lookAt - eye);
                    if (MATH::Length(viewForward) > kViewEpsilon) {
                        const float roll = relationshipRollRadians *
                            clampedRelationshipWeight;
                        const MATH::Vec3 viewRight = MATH::Normalize(
                            MATH::Cross(resolvedUp, viewForward));
                        resolvedUp = MATH::Normalize(
                            resolvedUp * std::cos(roll) +
                            viewRight * std::sin(roll));
                    }
                }
                submission.pose.up = resolvedUp;
                submission.pose.overrideVerticalFov =
                    relationshipShotResponseActive;
                submission.pose.verticalFovRadians = verticalFovRadians;
                submission.pose.modifierWeightScale =
                    relationshipShotResponseActive &&
                        relationshipSuppressModifiers
                    ? 1.0f - clampedRelationshipWeight
                    : 1.0f;
                if (rigService_->SubmitPose(submission)) {
                    RuntimeSceneContext::MarkCameraReady(
                        *cameraObject.GetWorld());
                }
            });
    }

} // namespace HIKARI
