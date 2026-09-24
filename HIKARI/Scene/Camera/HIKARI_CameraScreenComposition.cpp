#include "Scene/Camera/HIKARI_CameraScreenComposition.h"
#include "Scene/Camera/HIKARI_CameraFollowMath.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Scene/Components/HIKARI_CameraFollowComponent.h"

namespace HIKARI::CAMERA::FOLLOW {
namespace {
constexpr float kCompositionMinimumDepth = 0.05f;
constexpr float kCompositionPitchSearchRangeDegrees = 18.0f;
constexpr int kCompositionYawSearchSamples = 17;
constexpr int kCompositionPitchSearchSamples = 13;
constexpr float kInputEpsilon = 1.0e-4f;
constexpr float kViewEpsilon = 1.0e-5f;
constexpr float kMinimumGameplayAspect = 0.01f;
} // namespace

ViewportProjection ProjectPointToViewport(const MATH::Vec3& eye, const MATH::Vec3& lookAt, const MATH::Vec3& point,
                                          float verticalFovRadians, float aspect) noexcept {
    ViewportProjection result{};
    MATH::Vec3 forward = MATH::Normalize(lookAt - eye);
    if (!MATH::IsFinite(forward) || MATH::Length(forward) <= kViewEpsilon) {
        return result;
    }
    const auto basis = MATH::BuildViewBasisRH(forward, {0, 1, 0});
    const MATH::Vec3 right = basis.right;
    const MATH::Vec3 up = basis.up;
    const MATH::Vec3 relative = point - eye;
    const float depth = MATH::Dot(relative, forward);
    const float clampedFov = std::clamp(verticalFovRadians, DegreesToRadians(1.0f), DegreesToRadians(179.0f));
    const float tanHalfFov = std::tan(clampedFov * 0.5f);
    if (!std::isfinite(depth) || depth <= kCompositionMinimumDepth || !std::isfinite(tanHalfFov) ||
        tanHalfFov <= kViewEpsilon || !std::isfinite(aspect) || aspect < kMinimumGameplayAspect) {
        return result;
    }

    const float ndcX = MATH::Dot(relative, right) / (depth * tanHalfFov * aspect);
    const float ndcY = MATH::Dot(relative, up) / (depth * tanHalfFov);
    if (!std::isfinite(ndcX) || !std::isfinite(ndcY)) {
        return result;
    }
    result.x = ndcX * 0.5f + 0.5f;
    result.y = -ndcY * 0.5f + 0.5f;
    result.depth = depth;
    result.valid = true;
    return result;
}

ScreenCompositionTargets BuildCompositionTargets(const CameraFollowComponent& component, float side,
                                                 float framingWeight, float collisionBlend) noexcept {
    const bool secondaryOnRight = side >= 0.0f;
    const float basePrimaryX = secondaryOnRight ? component.Settings().screenCompositionPrimaryAnchorX
                                                : 1.0f - component.Settings().screenCompositionPrimaryAnchorX;
    const float baseSecondaryX = secondaryOnRight ? component.Settings().screenCompositionSecondaryAnchorX
                                                  : 1.0f - component.Settings().screenCompositionSecondaryAnchorX;
    const float safePrimaryX = secondaryOnRight ? 0.45f : 0.55f;
    const float safeSecondaryX = secondaryOnRight ? 0.55f : 0.45f;
    const float weight = std::clamp(framingWeight, 0.0f, 1.0f);
    const float collision =
        std::clamp(collisionBlend * component.Settings().screenCompositionCollisionCenteringStrength, 0.0f, 1.0f);

    ScreenCompositionTargets result{};
    const float primaryX = Lerp(0.5f, basePrimaryX, weight);
    const float secondaryX = Lerp(0.5f, baseSecondaryX, weight);
    result.primaryX = Lerp(primaryX, Lerp(0.5f, safePrimaryX, weight), collision);
    result.secondaryX = Lerp(secondaryX, Lerp(0.5f, safeSecondaryX, weight), collision);
    result.primaryY = Lerp(Lerp(0.5f, component.Settings().screenCompositionPrimaryAnchorY, weight),
                           Lerp(0.5f, 0.54f, weight), collision);
    result.secondaryY = Lerp(Lerp(0.5f, component.Settings().screenCompositionSecondaryAnchorY, weight),
                             Lerp(0.5f, 0.49f, weight), collision);
    result.softHalfWidth =
        std::clamp(component.Settings().screenCompositionSoftZoneHalfWidth + 0.035f * collision, 0.0f, 0.48f);
    result.softHalfHeight =
        std::clamp(component.Settings().screenCompositionSoftZoneHalfHeight + 0.030f * collision, 0.0f, 0.48f);
    result.hardHalfWidth = std::clamp(component.Settings().screenCompositionHardZoneHalfWidth + 0.025f * collision,
                                      result.softHalfWidth + 0.01f, 0.50f);
    result.hardHalfHeight = std::clamp(component.Settings().screenCompositionHardZoneHalfHeight + 0.025f * collision,
                                       result.softHalfHeight + 0.01f, 0.50f);
    return result;
}

float AccumulateZoneCost(float value, float anchor, float softHalfExtent, float hardHalfExtent, bool& outSoftViolation,
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
    const float range = (std::max)(hardHalfExtent - softHalfExtent, 0.01f);
    float normalized = (error - softHalfExtent) / range;
    if (normalized > 1.0f) {
        normalized = 1.0f + (normalized - 1.0f) * 2.5f;
    }
    return normalized * normalized;
}

ScreenCompositionEvaluation EvaluateScreenComposition(const MATH::Vec3& primary, const MATH::Vec3& secondary,
                                                      const MATH::Vec3& eye, const MATH::Vec3& lookAt,
                                                      float verticalFovRadians, float aspect,
                                                      const ScreenCompositionTargets& targets) noexcept {
    ScreenCompositionEvaluation result{};
    result.primary = ProjectPointToViewport(eye, lookAt, primary, verticalFovRadians, aspect);
    result.secondary = ProjectPointToViewport(eye, lookAt, secondary, verticalFovRadians, aspect);
    if (!result.primary.valid || !result.secondary.valid) {
        return result;
    }

    result.horizontalSoftViolation = false;
    result.horizontalHardViolation = false;
    result.verticalSoftViolation = false;
    result.verticalHardViolation = false;
    float horizontalCost = 0.0f;
    horizontalCost +=
        AccumulateZoneCost(result.primary.x, targets.primaryX, targets.softHalfWidth, targets.hardHalfWidth,
                           result.horizontalSoftViolation, result.horizontalHardViolation);
    horizontalCost +=
        AccumulateZoneCost(result.secondary.x, targets.secondaryX, targets.softHalfWidth, targets.hardHalfWidth,
                           result.horizontalSoftViolation, result.horizontalHardViolation);
    float verticalCost = 0.0f;
    verticalCost +=
        AccumulateZoneCost(result.primary.y, targets.primaryY, targets.softHalfHeight, targets.hardHalfHeight,
                           result.verticalSoftViolation, result.verticalHardViolation);
    verticalCost +=
        AccumulateZoneCost(result.secondary.y, targets.secondaryY, targets.softHalfHeight, targets.hardHalfHeight,
                           result.verticalSoftViolation, result.verticalHardViolation);
    result.cost = horizontalCost + verticalCost * 0.60f;
    return result;
}

float SearchCompositionYaw(const CameraFollowComponent& component, const MATH::Vec3& primary,
                           const MATH::Vec3& secondary, const MATH::Vec3& lookAt, float relationYaw, float side,
                           float pitch, float distance, float verticalFovRadians, float aspect, float framingWeight,
                           float collisionBlend, const ScreenCompositionTargets& targets) noexcept {
    const float collisionDamping =
        1.0f - std::clamp(collisionBlend * component.Settings().screenCompositionCollisionCenteringStrength * 0.72f,
                          0.0f, 0.90f);
    const float maximumAngle =
        component.Settings().screenCompositionMaximumShoulderAngleDegrees * framingWeight * collisionDamping;
    const float preferredAngle =
        std::min(maximumAngle, component.Settings().screenCompositionPreferredShoulderAngleDegrees * framingWeight *
                                   collisionDamping);
    if (maximumAngle <= kInputEpsilon) {
        return relationYaw;
    }

    float bestYaw = relationYaw + side * preferredAngle;
    float bestCost = std::numeric_limits<float>::max();
    float bestOffset = preferredAngle;
    const auto considerOffset = [&](float offset) {
        const float candidateYaw = relationYaw + side * offset;
        const MATH::Vec3 eye = primary + OrbitDirection(candidateYaw, pitch) * distance;
        const ScreenCompositionEvaluation evaluation =
            EvaluateScreenComposition(primary, secondary, eye, lookAt, verticalFovRadians, aspect, targets);
        const float regularization = maximumAngle > kViewEpsilon ? (offset - preferredAngle) / maximumAngle : 0.0f;
        const float cost = evaluation.cost + regularization * regularization * 0.08f;
        if (cost < bestCost) {
            bestCost = cost;
            bestOffset = offset;
            bestYaw = candidateYaw;
        }
    };
    for (int sample = 0; sample < kCompositionYawSearchSamples; ++sample) {
        const float amount = static_cast<float>(sample) / static_cast<float>(kCompositionYawSearchSamples - 1);
        const float offset = maximumAngle * amount;
        considerOffset(offset);
    }

    float step = maximumAngle / static_cast<float>(kCompositionYawSearchSamples - 1);
    for (int iteration = 0; iteration < 3; ++iteration) {
        for (float direction : {-1.0f, 1.0f}) {
            const float offset = std::clamp(bestOffset + direction * step, 0.0f, maximumAngle);
            considerOffset(offset);
        }
        step *= 0.5f;
    }
    return WrapDegrees(bestYaw);
}

float SearchCompositionPitch(const CameraFollowComponent& component, const MATH::Vec3& primary,
                             const MATH::Vec3& secondary, const MATH::Vec3& lookAt, float yaw, float preferredPitch,
                             float distance, float verticalFovRadians, float aspect, float framingWeight,
                             const ScreenCompositionTargets& targets) noexcept {
    const float range = kCompositionPitchSearchRangeDegrees * std::clamp(framingWeight, 0.0f, 1.0f);
    const float minimumPitch = std::clamp(preferredPitch - range, component.Settings().minimumPitchDegrees,
                                          component.Settings().maximumPitchDegrees);
    const float maximumPitch =
        std::clamp(preferredPitch + range, minimumPitch, component.Settings().maximumPitchDegrees);
    float bestPitch = std::clamp(preferredPitch, minimumPitch, maximumPitch);
    float bestCost = std::numeric_limits<float>::max();
    for (int sample = 0; sample < kCompositionPitchSearchSamples; ++sample) {
        const float amount = static_cast<float>(sample) / static_cast<float>(kCompositionPitchSearchSamples - 1);
        const float candidatePitch = Lerp(minimumPitch, maximumPitch, amount);
        const MATH::Vec3 eye = primary + OrbitDirection(yaw, candidatePitch) * distance;
        const ScreenCompositionEvaluation evaluation =
            EvaluateScreenComposition(primary, secondary, eye, lookAt, verticalFovRadians, aspect, targets);
        const float regularization = range > kViewEpsilon ? (candidatePitch - preferredPitch) / range : 0.0f;
        const float cost = evaluation.cost + regularization * regularization * 0.10f;
        if (cost < bestCost) {
            bestCost = cost;
            bestPitch = candidatePitch;
        }
    }
    return bestPitch;
}

} // namespace HIKARI::CAMERA::FOLLOW
