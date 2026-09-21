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
        std::string_view GetTypeName() const override {
            return "CameraFollowComponent";
        }

        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

        bool IsEnabled() const noexcept { return enabled_; }
        int GetPriority() const noexcept { return priority_; }
        SceneObjectId GetTargetObjectId() const noexcept {
            return targetObjectId_;
        }
        bool GetUseOwnerAsFallbackTarget() const noexcept {
            return useOwnerAsFallbackTarget_;
        }
        const MATH::Vec3& GetPivotOffset() const noexcept {
            return Settings().pivotOffset;
        }
        float GetInitialYawDegrees() const noexcept;
        float GetInitialPitchDegrees() const noexcept;
        float GetInitialDistance() const noexcept;
        float GetMinimumPitchDegrees() const noexcept;
        float GetMaximumPitchDegrees() const noexcept;
        float GetMinimumDistance() const noexcept;
        float GetMaximumDistance() const noexcept;
        float GetFollowSmooth() const noexcept;
        float GetLookSmooth() const noexcept;
        float GetDistanceSmooth() const noexcept;

        bool IsOrbitInputEnabled() const noexcept {
            return Settings().orbitInputEnabled;
        }
        const std::string& GetLookActionId() const noexcept {
            return Settings().lookActionId;
        }
        const std::string& GetZoomActionId() const noexcept {
            return Settings().zoomActionId;
        }
        const std::string& GetRecenterActionId() const noexcept {
            return Settings().recenterActionId;
        }
        float GetYawSpeedDegreesPerSecond() const noexcept;
        float GetPitchSpeedDegreesPerSecond() const noexcept;
        float GetMouseSensitivityDegreesPerPixel() const noexcept;
        float GetZoomSpeedUnitsPerSecond() const noexcept;
        float GetMouseWheelZoomUnitsPerStep() const noexcept;
        bool GetInvertVerticalLook() const noexcept {
            return Settings().invertVerticalLook;
        }
        bool IsAutoRecenterEnabled() const noexcept {
            return Settings().autoRecenterEnabled;
        }
        float GetAutoRecenterDelaySeconds() const noexcept;
        float GetAutoRecenterSpeedDegreesPerSecond() const noexcept;

        bool IsCollisionEnabled() const noexcept {
            return Settings().collisionEnabled;
        }
        bool GetCollisionStaticOnly() const noexcept {
            return Settings().collisionStaticOnly;
        }
        float GetCollisionRadius() const noexcept;
        float GetCollisionPadding() const noexcept;
        uint32_t GetCollisionLayerMask() const noexcept {
            return Settings().collisionLayerMask;
        }

        bool IsScreenCompositionEnabled() const noexcept {
            return Settings().screenCompositionEnabled;
        }
        float GetScreenCompositionPrimaryAnchorX() const noexcept {
            return Settings().screenCompositionPrimaryAnchorX;
        }
        float GetScreenCompositionPrimaryAnchorY() const noexcept {
            return Settings().screenCompositionPrimaryAnchorY;
        }
        float GetScreenCompositionSecondaryAnchorX() const noexcept {
            return Settings().screenCompositionSecondaryAnchorX;
        }
        float GetScreenCompositionSecondaryAnchorY() const noexcept {
            return Settings().screenCompositionSecondaryAnchorY;
        }
        float GetScreenCompositionSoftZoneHalfWidth() const noexcept {
            return Settings().screenCompositionSoftZoneHalfWidth;
        }
        float GetScreenCompositionSoftZoneHalfHeight() const noexcept {
            return Settings().screenCompositionSoftZoneHalfHeight;
        }
        float GetScreenCompositionHardZoneHalfWidth() const noexcept {
            return Settings().screenCompositionHardZoneHalfWidth;
        }
        float GetScreenCompositionHardZoneHalfHeight() const noexcept {
            return Settings().screenCompositionHardZoneHalfHeight;
        }
        float GetScreenCompositionPreferredShoulderAngleDegrees()
            const noexcept {
            return Settings().screenCompositionPreferredShoulderAngleDegrees;
        }
        float GetScreenCompositionMaximumShoulderAngleDegrees()
            const noexcept {
            return Settings().screenCompositionMaximumShoulderAngleDegrees;
        }
        float GetScreenCompositionFocusHeightBias() const noexcept {
            return Settings().screenCompositionFocusHeightBias;
        }
        float GetScreenCompositionHardResponseScale() const noexcept {
            return Settings().screenCompositionHardResponseScale;
        }
        float GetScreenCompositionCollisionCenteringStrength()
            const noexcept {
            return Settings().screenCompositionCollisionCenteringStrength;
        }

        bool HasRuntimeState() const noexcept { return runtimeInitialized_; }
        float GetRuntimeYawDegrees() const noexcept { return runtimeYawDegrees_; }
        float GetRuntimePitchDegrees() const noexcept { return runtimePitchDegrees_; }
        float GetRuntimeDistance() const noexcept { return runtimeDistance_; }
        float GetRuntimeDesiredDistance() const noexcept {
            return runtimeDesiredDistance_;
        }
        float GetRuntimeLookIdleSeconds() const noexcept {
            return runtimeLookIdleSeconds_;
        }
        const MATH::Vec3& GetRuntimeEye() const noexcept {
            return runtimeEye_;
        }
        const MATH::Vec3& GetRuntimeLookAt() const noexcept {
            return runtimeLookAt_;
        }
        const MATH::Vec3& GetRuntimeFollowPivot() const noexcept {
            return runtimeFollowPivot_;
        }
        const MATH::Vec3& GetRuntimeTargetPivot() const noexcept {
            return runtimeTargetPivot_;
        }
        bool WasCollisionLimited() const noexcept {
            return runtimeCollisionLimited_;
        }

        bool HasRuntimeFramingState() const noexcept {
            return runtimeFramingInitialized_;
        }
        bool HasRuntimeFramingSubmission() const noexcept {
            return runtimeFramingHasSubmission_;
        }
        RuntimeObjectHandle GetRuntimeFramingTargetObject() const noexcept {
            return runtimeFramingTargetObject_;
        }
        CAMERA::CameraRigSourceId GetRuntimeFramingSourceId() const noexcept {
            return runtimeFramingSourceId_;
        }
        int GetRuntimeFramingPriority() const noexcept {
            return runtimeFramingPriority_;
        }
        const CAMERA::CameraTargetFramingDynamics&
            GetRuntimeFramingDynamics() const noexcept {
            return runtimeFramingDynamics_;
        }
        float GetRuntimeFramingTargetWeight() const noexcept {
            return runtimeFramingTargetWeight_;
        }
        float GetRuntimeFramingDistanceScale() const noexcept {
            return runtimeFramingDistanceScale_;
        }
        float GetRuntimeFramingMaximumDistanceOffset() const noexcept {
            return runtimeFramingMaximumDistanceOffset_;
        }
        float GetRuntimeFramingYawTrackingSpeed() const noexcept {
            return runtimeFramingYawTrackingSpeed_;
        }
        float GetRuntimeFramingPreferredPitch() const noexcept {
            return runtimeFramingPreferredPitchDegrees_;
        }
        float GetRuntimeFramingPitchTrackingSpeed() const noexcept {
            return runtimeFramingPitchTrackingSpeed_;
        }
        bool GetRuntimeFramingSuppressOrbitInput() const noexcept {
            return runtimeFramingSuppressOrbitInput_;
        }
        bool GetRuntimeFramingSuppressZoomInput() const noexcept {
            return runtimeFramingSuppressZoomInput_;
        }
        bool GetRuntimeFramingSwitchActive() const noexcept {
            return runtimeFramingSwitchActive_;
        }
        float GetRuntimeFramingSwitchRemainingSeconds() const noexcept {
            return runtimeFramingSwitchRemainingSeconds_;
        }
        float GetRuntimeFramingWeight() const noexcept {
            return runtimeFramingWeight_;
        }
        float GetRuntimeFramingWeightVelocity() const noexcept {
            return runtimeFramingWeightVelocity_;
        }
        const MATH::Vec3& GetRuntimeFramingSecondaryPivot() const noexcept {
            return runtimeFramingSecondaryPivot_;
        }
        const MATH::Vec3&
            GetRuntimeFramingSecondaryPivotVelocity() const noexcept {
            return runtimeFramingSecondaryPivotVelocity_;
        }
        float GetRuntimeFramingYawVelocity() const noexcept {
            return runtimeFramingYawVelocity_;
        }
        float GetRuntimeFramingPitchVelocity() const noexcept {
            return runtimeFramingPitchVelocity_;
        }
        const MATH::Vec3& GetRuntimeFramingFocusOffset() const noexcept {
            return runtimeFramingFocusOffset_;
        }
        const MATH::Vec3& GetRuntimeFramingFocusVelocity() const noexcept {
            return runtimeFramingFocusVelocity_;
        }
        float GetRuntimeFramingDistanceVelocity() const noexcept {
            return runtimeFramingDistanceVelocity_;
        }
        bool HasRuntimeRelationshipShotState() const noexcept {
            return runtimeRelationshipShotInitialized_;
        }
        bool HasRuntimeRelationshipShotSubmission() const noexcept {
            return runtimeRelationshipShotHasSubmission_;
        }
        float GetRuntimeRelationshipPreferredSide() const noexcept {
            return runtimeRelationshipPreferredSide_;
        }
        float GetRuntimeRelationshipShoulderAngleDegrees() const noexcept {
            return runtimeRelationshipShoulderAngleDegrees_;
        }
        float GetRuntimeRelationshipVerticalFovOffsetRadians() const noexcept {
            return runtimeRelationshipVerticalFovOffsetRadians_;
        }
        float GetRuntimeRelationshipRollRadians() const noexcept {
            return runtimeRelationshipRollRadians_;
        }
        float GetRuntimeRelationshipSideClearanceBias() const noexcept {
            return runtimeRelationshipSideClearanceBias_;
        }
        float GetRuntimeRelationshipDistanceOffset() const noexcept {
            return runtimeRelationshipDistanceOffset_;
        }
        float GetRuntimeRelationshipPivotHeightOffset() const noexcept {
            return runtimeRelationshipPivotHeightOffset_;
        }
        float GetRuntimeRelationshipFocusLateralOffset() const noexcept {
            return runtimeRelationshipFocusLateralOffset_;
        }
        float GetRuntimeRelationshipFocusHeightOffset() const noexcept {
            return runtimeRelationshipFocusHeightOffset_;
        }
        bool GetRuntimeRelationshipSuppressModifiers() const noexcept {
            return runtimeRelationshipSuppressModifiers_;
        }
        float GetRuntimeRelationshipYawOffsetDegrees() const noexcept {
            return runtimeRelationshipYawOffsetDegrees_;
        }
        float GetRuntimeRelationshipWeight() const noexcept {
            return runtimeRelationshipWeight_;
        }
        float GetRuntimeRelationshipWeightVelocity() const noexcept {
            return runtimeRelationshipWeightVelocity_;
        }
        bool IsRuntimeScreenCompositionActive() const noexcept {
            return runtimeScreenCompositionActive_;
        }
        float GetRuntimeScreenCompositionSide() const noexcept {
            return runtimeScreenCompositionSide_;
        }
        float GetRuntimeScreenCompositionPrimaryX() const noexcept {
            return runtimeScreenCompositionPrimaryX_;
        }
        float GetRuntimeScreenCompositionPrimaryY() const noexcept {
            return runtimeScreenCompositionPrimaryY_;
        }
        float GetRuntimeScreenCompositionSecondaryX() const noexcept {
            return runtimeScreenCompositionSecondaryX_;
        }
        float GetRuntimeScreenCompositionSecondaryY() const noexcept {
            return runtimeScreenCompositionSecondaryY_;
        }
        bool GetRuntimeScreenCompositionSoftViolation() const noexcept {
            return runtimeScreenCompositionSoftViolation_;
        }
        bool GetRuntimeScreenCompositionHardViolation() const noexcept {
            return runtimeScreenCompositionHardViolation_;
        }
        float GetRuntimeScreenCompositionCollisionBlend() const noexcept {
            return runtimeScreenCompositionCollisionBlend_;
        }
        float GetRuntimeScreenCompositionCollisionBlendVelocity()
            const noexcept {
            return runtimeScreenCompositionCollisionBlendVelocity_;
        }

        void InitializeRuntimeState(
            const MATH::Vec3& eye,
            const MATH::Vec3& lookAt) noexcept;
        void SetRuntimeOrbit(
            float yawDegrees,
            float pitchDegrees,
            float desiredDistance,
            float resolvedDistance,
            float lookIdleSeconds) noexcept;
        void SetRuntimeView(
            const MATH::Vec3& eye,
            const MATH::Vec3& lookAt,
            bool collisionLimited) noexcept;
        void SetRuntimeTracking(
            const MATH::Vec3& followPivot,
            const MATH::Vec3& targetPivot) noexcept;
        void SetRuntimeFramingState(
            bool hasSubmission,
            RuntimeObjectHandle targetObject,
            CAMERA::CameraRigSourceId sourceId,
            int priority,
            const CAMERA::CameraTargetFramingDynamics& dynamics,
            float targetWeight,
            float distanceScale,
            float maximumDistanceOffset,
            float yawTrackingSpeed,
            float preferredPitchDegrees,
            float pitchTrackingSpeed,
            bool suppressOrbitInput,
            bool suppressZoomInput,
            bool switchActive,
            float switchRemainingSeconds,
            const MATH::Vec3& secondaryPivot,
            float framingWeight,
            float framingWeightVelocity,
            const MATH::Vec3& secondaryPivotVelocity,
            float yawVelocity,
            float pitchVelocity,
            const MATH::Vec3& focusOffset,
            const MATH::Vec3& focusVelocity,
            float distanceVelocity) noexcept;
        void SetRuntimeRelationshipShotState(
            bool hasSubmission,
            float preferredSide,
            float shoulderAngleDegrees,
            float verticalFovOffsetRadians,
            float rollRadians,
            float sideClearanceBias,
            float distanceOffset,
            float pivotHeightOffset,
            float focusLateralOffset,
            float focusHeightOffset,
            bool suppressModifiers,
            float yawOffsetDegrees,
            float weight,
            float weightVelocity) noexcept;
        void SetRuntimeScreenCompositionState(
            bool active,
            float side,
            float primaryX,
            float primaryY,
            float secondaryX,
            float secondaryY,
            bool softViolation,
            bool hardViolation,
            float collisionBlend,
            float collisionBlendVelocity) noexcept;
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

        bool runtimeInitialized_ = false;
        float runtimeYawDegrees_ = 0.0f;
        float runtimePitchDegrees_ = 0.0f;
        float runtimeDistance_ = 0.0f;
        float runtimeDesiredDistance_ = 0.0f;
        float runtimeLookIdleSeconds_ = 0.0f;
        MATH::Vec3 runtimeEye_{};
        MATH::Vec3 runtimeLookAt_{};
        MATH::Vec3 runtimeFollowPivot_{};
        MATH::Vec3 runtimeTargetPivot_{};
        bool runtimeCollisionLimited_ = false;

        bool runtimeFramingInitialized_ = false;
        bool runtimeFramingHasSubmission_ = false;
        RuntimeObjectHandle runtimeFramingTargetObject_{};
        CAMERA::CameraRigSourceId runtimeFramingSourceId_ = 0u;
        int runtimeFramingPriority_ = 0;
        CAMERA::CameraTargetFramingDynamics runtimeFramingDynamics_{};
        float runtimeFramingTargetWeight_ = 0.45f;
        float runtimeFramingDistanceScale_ = 0.18f;
        float runtimeFramingMaximumDistanceOffset_ = 2.0f;
        float runtimeFramingYawTrackingSpeed_ = 540.0f;
        float runtimeFramingPreferredPitchDegrees_ = 17.0f;
        float runtimeFramingPitchTrackingSpeed_ = 240.0f;
        bool runtimeFramingSuppressOrbitInput_ = true;
        bool runtimeFramingSuppressZoomInput_ = true;
        bool runtimeFramingSwitchActive_ = false;
        float runtimeFramingSwitchRemainingSeconds_ = 0.0f;
        float runtimeFramingWeight_ = 0.0f;
        float runtimeFramingWeightVelocity_ = 0.0f;
        MATH::Vec3 runtimeFramingSecondaryPivot_{};
        MATH::Vec3 runtimeFramingSecondaryPivotVelocity_{};
        float runtimeFramingYawVelocity_ = 0.0f;
        float runtimeFramingPitchVelocity_ = 0.0f;
        MATH::Vec3 runtimeFramingFocusOffset_{};
        MATH::Vec3 runtimeFramingFocusVelocity_{};
        float runtimeFramingDistanceVelocity_ = 0.0f;

        bool runtimeRelationshipShotInitialized_ = false;
        bool runtimeRelationshipShotHasSubmission_ = false;
        float runtimeRelationshipPreferredSide_ = 0.0f;
        float runtimeRelationshipShoulderAngleDegrees_ = 0.0f;
        float runtimeRelationshipVerticalFovOffsetRadians_ = 0.0f;
        float runtimeRelationshipRollRadians_ = 0.0f;
        float runtimeRelationshipSideClearanceBias_ = 0.0f;
        float runtimeRelationshipDistanceOffset_ = 0.0f;
        float runtimeRelationshipPivotHeightOffset_ = 0.0f;
        float runtimeRelationshipFocusLateralOffset_ = 0.0f;
        float runtimeRelationshipFocusHeightOffset_ = 0.0f;
        bool runtimeRelationshipSuppressModifiers_ = false;
        float runtimeRelationshipYawOffsetDegrees_ = 0.0f;
        float runtimeRelationshipWeight_ = 0.0f;
        float runtimeRelationshipWeightVelocity_ = 0.0f;

        bool runtimeScreenCompositionActive_ = false;
        float runtimeScreenCompositionSide_ = 1.0f;
        float runtimeScreenCompositionPrimaryX_ = 0.5f;
        float runtimeScreenCompositionPrimaryY_ = 0.5f;
        float runtimeScreenCompositionSecondaryX_ = 0.5f;
        float runtimeScreenCompositionSecondaryY_ = 0.5f;
        bool runtimeScreenCompositionSoftViolation_ = false;
        bool runtimeScreenCompositionHardViolation_ = false;
        float runtimeScreenCompositionCollisionBlend_ = 0.0f;
        float runtimeScreenCompositionCollisionBlendVelocity_ = 0.0f;
    };

} // namespace HIKARI
