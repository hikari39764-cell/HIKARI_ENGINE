#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include "Scene/Camera/HIKARI_GameplayCameraEvaluation.h"
#include "Scene/Camera/HIKARI_CameraFollowSettings.h"

#include "HIKARI_IComponent.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Scene/Camera/HIKARI_CameraRigTypes.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

class CameraFollowComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "CameraFollowComponent"; }

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
    void BuildInspector(IInspectorBuilder& builder) override;

    bool IsEnabled() const noexcept { return enabled_; }
    int GetPriority() const noexcept { return priority_; }
    SceneObjectId GetTargetObjectId() const noexcept { return targetObjectId_; }
    bool GetUseOwnerAsFallbackTarget() const noexcept { return useOwnerAsFallbackTarget_; }
    float GetInitialPitchDegrees() const noexcept;
    float GetInitialDistance() const noexcept;

    struct RuntimeState {
        bool initialized = false;
        float yawDegrees = 0.0f;
        float pitchDegrees = 0.0f;
        float distance = 0.0f;
        float desiredDistance = 0.0f;
        float lookIdleSeconds = 0.0f;
        MATH::Vec3 eye{};
        MATH::Vec3 lookAt{};
        MATH::Vec3 followPivot{};
        MATH::Vec3 targetPivot{};
        bool collisionLimited = false;

        bool framingInitialized = false;
        bool framingHasSubmission = false;
        RuntimeObjectHandle framingTargetObject{};
        CAMERA::CameraRigSourceId framingSourceId = 0u;
        int framingPriority = 0;
        CAMERA::CameraTargetFramingDynamics framingDynamics{};
        float framingTargetWeight = 0.45f;
        float framingDistanceScale = 0.18f;
        float framingMaximumDistanceOffset = 2.0f;
        float framingYawTrackingSpeed = 540.0f;
        float framingPreferredPitchDegrees = 17.0f;
        float framingPitchTrackingSpeed = 240.0f;
        bool framingSuppressOrbitInput = true;
        bool framingSuppressZoomInput = true;
        bool framingSwitchActive = false;
        float framingSwitchRemainingSeconds = 0.0f;
        float framingWeight = 0.0f;
        float framingWeightVelocity = 0.0f;
        MATH::Vec3 framingSecondaryPivot{};
        MATH::Vec3 framingSecondaryPivotVelocity{};
        float framingYawVelocity = 0.0f;
        float framingPitchVelocity = 0.0f;
        MATH::Vec3 framingFocusOffset{};
        MATH::Vec3 framingFocusVelocity{};
        float framingDistanceVelocity = 0.0f;

        bool relationshipShotInitialized = false;
        bool relationshipShotHasSubmission = false;
        float relationshipPreferredSide = 0.0f;
        float relationshipShoulderAngleDegrees = 0.0f;
        float relationshipVerticalFovOffsetRadians = 0.0f;
        float relationshipRollRadians = 0.0f;
        float relationshipSideClearanceBias = 0.0f;
        float relationshipDistanceOffset = 0.0f;
        float relationshipPivotHeightOffset = 0.0f;
        float relationshipFocusLateralOffset = 0.0f;
        float relationshipFocusHeightOffset = 0.0f;
        bool relationshipSuppressModifiers = false;
        float relationshipYawOffsetDegrees = 0.0f;
        float relationshipWeight = 0.0f;
        float relationshipWeightVelocity = 0.0f;

        bool screenCompositionActive = false;
        float screenCompositionSide = 1.0f;
        float screenCompositionPrimaryX = 0.5f;
        float screenCompositionPrimaryY = 0.5f;
        float screenCompositionSecondaryX = 0.5f;
        float screenCompositionSecondaryY = 0.5f;
        bool screenCompositionSoftViolation = false;
        bool screenCompositionHardViolation = false;
        float screenCompositionCollisionBlend = 0.0f;
        float screenCompositionCollisionBlendVelocity = 0.0f;
    };

    const RuntimeState& Runtime() const noexcept { return runtime_; }

    void InitializeRuntimeState(const MATH::Vec3& eye, const MATH::Vec3& lookAt) noexcept;
    void SetRuntimeOrbit(float yawDegrees, float pitchDegrees, float desiredDistance, float resolvedDistance,
                         float lookIdleSeconds) noexcept;
    void SetRuntimeView(const MATH::Vec3& eye, const MATH::Vec3& lookAt, bool collisionLimited) noexcept;
    void SetRuntimeTracking(const MATH::Vec3& followPivot, const MATH::Vec3& targetPivot) noexcept;
    void SetRuntimeFramingState(bool hasSubmission, RuntimeObjectHandle targetObject,
                                CAMERA::CameraRigSourceId sourceId, int priority,
                                const CAMERA::CameraTargetFramingDynamics& dynamics, float targetWeight,
                                float distanceScale, float maximumDistanceOffset, float yawTrackingSpeed,
                                float preferredPitchDegrees, float pitchTrackingSpeed, bool suppressOrbitInput,
                                bool suppressZoomInput, bool switchActive, float switchRemainingSeconds,
                                const MATH::Vec3& secondaryPivot, float framingWeight, float framingWeightVelocity,
                                const MATH::Vec3& secondaryPivotVelocity, float yawVelocity, float pitchVelocity,
                                const MATH::Vec3& focusOffset, const MATH::Vec3& focusVelocity,
                                float distanceVelocity) noexcept;
    void SetRuntimeRelationshipShotState(bool hasSubmission, float preferredSide, float shoulderAngleDegrees,
                                         float verticalFovOffsetRadians, float rollRadians, float sideClearanceBias,
                                         float distanceOffset, float pivotHeightOffset, float focusLateralOffset,
                                         float focusHeightOffset, bool suppressModifiers, float yawOffsetDegrees,
                                         float weight, float weightVelocity) noexcept;
    void SetRuntimeScreenCompositionState(bool active, float side, float primaryX, float primaryY, float secondaryX,
                                          float secondaryY, bool softViolation, bool hardViolation,
                                          float collisionBlend, float collisionBlendVelocity) noexcept;
    CAMERA::GameplayCameraBinding& GameplayBinding() noexcept { return gameplayBinding_; }
    const CAMERA::GameplayCameraBinding& GameplayBinding() const noexcept { return gameplayBinding_; }
    CAMERA::GameplayCameraEvaluation& GameplayState() noexcept { return gameplayState_; }
    const CAMERA::GameplayCameraEvaluation& GameplayState() const noexcept { return gameplayState_; }
    void ResetRuntimeState() noexcept;
    const CAMERA::CameraFollowSettings& Settings() const noexcept {
        return evaluatedSettings_ ? *evaluatedSettings_ : settings_;
    }
    const CAMERA::CameraFollowSettings& AuthoredSettings() const noexcept { return settings_; }
    void SetEvaluatedSettings(const CAMERA::CameraFollowSettings& settings) { evaluatedSettings_ = settings; }
    void ClearEvaluatedSettings() noexcept { evaluatedSettings_.reset(); }

private:
    void ClampSettings() noexcept;

    bool enabled_ = true;
    int priority_ = 0;
    SceneObjectId targetObjectId_{};
    bool useOwnerAsFallbackTarget_ = true;
    CAMERA::GameplayCameraBinding gameplayBinding_;
    CAMERA::GameplayCameraEvaluation gameplayState_;
    CAMERA::CameraFollowSettings settings_{};
    std::optional<CAMERA::CameraFollowSettings> evaluatedSettings_;

    RuntimeState runtime_{};
};

} // namespace HIKARI
