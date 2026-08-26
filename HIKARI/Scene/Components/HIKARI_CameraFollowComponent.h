#pragma once

#include <cstdint>
#include <string>
#include <string_view>

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
            return pivotOffset_;
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
            return orbitInputEnabled_;
        }
        const std::string& GetLookActionId() const noexcept {
            return lookActionId_;
        }
        const std::string& GetZoomActionId() const noexcept {
            return zoomActionId_;
        }
        const std::string& GetRecenterActionId() const noexcept {
            return recenterActionId_;
        }
        float GetYawSpeedDegreesPerSecond() const noexcept;
        float GetPitchSpeedDegreesPerSecond() const noexcept;
        float GetMouseSensitivityDegreesPerPixel() const noexcept;
        float GetZoomSpeedUnitsPerSecond() const noexcept;
        float GetMouseWheelZoomUnitsPerStep() const noexcept;
        bool GetInvertVerticalLook() const noexcept {
            return invertVerticalLook_;
        }
        bool IsAutoRecenterEnabled() const noexcept {
            return autoRecenterEnabled_;
        }
        float GetAutoRecenterDelaySeconds() const noexcept;
        float GetAutoRecenterSpeedDegreesPerSecond() const noexcept;

        bool IsCollisionEnabled() const noexcept {
            return collisionEnabled_;
        }
        bool GetCollisionStaticOnly() const noexcept {
            return collisionStaticOnly_;
        }
        float GetCollisionRadius() const noexcept;
        float GetCollisionPadding() const noexcept;
        uint32_t GetCollisionLayerMask() const noexcept {
            return collisionLayerMask_;
        }

        bool IsScreenCompositionEnabled() const noexcept {
            return screenCompositionEnabled_;
        }
        float GetScreenCompositionPrimaryAnchorX() const noexcept {
            return screenCompositionPrimaryAnchorX_;
        }
        float GetScreenCompositionPrimaryAnchorY() const noexcept {
            return screenCompositionPrimaryAnchorY_;
        }
        float GetScreenCompositionSecondaryAnchorX() const noexcept {
            return screenCompositionSecondaryAnchorX_;
        }
        float GetScreenCompositionSecondaryAnchorY() const noexcept {
            return screenCompositionSecondaryAnchorY_;
        }
        float GetScreenCompositionSoftZoneHalfWidth() const noexcept {
            return screenCompositionSoftZoneHalfWidth_;
        }
        float GetScreenCompositionSoftZoneHalfHeight() const noexcept {
            return screenCompositionSoftZoneHalfHeight_;
        }
        float GetScreenCompositionHardZoneHalfWidth() const noexcept {
            return screenCompositionHardZoneHalfWidth_;
        }
        float GetScreenCompositionHardZoneHalfHeight() const noexcept {
            return screenCompositionHardZoneHalfHeight_;
        }
        float GetScreenCompositionPreferredShoulderAngleDegrees()
            const noexcept {
            return screenCompositionPreferredShoulderAngleDegrees_;
        }
        float GetScreenCompositionMaximumShoulderAngleDegrees()
            const noexcept {
            return screenCompositionMaximumShoulderAngleDegrees_;
        }
        float GetScreenCompositionFocusHeightBias() const noexcept {
            return screenCompositionFocusHeightBias_;
        }
        float GetScreenCompositionHardResponseScale() const noexcept {
            return screenCompositionHardResponseScale_;
        }
        float GetScreenCompositionCollisionCenteringStrength()
            const noexcept {
            return screenCompositionCollisionCenteringStrength_;
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
        void ResetRuntimeState() noexcept;

    private:
        void ClampSettings() noexcept;

        bool enabled_ = true;
        int priority_ = 0;
        SceneObjectId targetObjectId_{};
        bool useOwnerAsFallbackTarget_ = true;
        MATH::Vec3 pivotOffset_{ 0.0f, 1.2f, 0.0f };
        float initialYawDegrees_ = 0.0f;
        float initialPitchDegrees_ = 28.0f;
        float initialDistance_ = 8.0f;
        float minimumPitchDegrees_ = -65.0f;
        float maximumPitchDegrees_ = 75.0f;
        float minimumDistance_ = 1.0f;
        float maximumDistance_ = 20.0f;
        float followSmooth_ = 12.0f;
        float lookSmooth_ = 14.0f;
        float distanceSmooth_ = 18.0f;

        bool orbitInputEnabled_ = true;
        std::string lookActionId_{ "Gameplay.Look" };
        std::string zoomActionId_{ "Gameplay.CameraZoom" };
        std::string recenterActionId_{ "Gameplay.CameraRecenter" };
        float yawSpeedDegreesPerSecond_ = 180.0f;
        float pitchSpeedDegreesPerSecond_ = 140.0f;
        float mouseSensitivityDegreesPerPixel_ = 0.12f;
        float zoomSpeedUnitsPerSecond_ = 30.0f;
        float mouseWheelZoomUnitsPerStep_ = 1.0f;
        bool invertVerticalLook_ = false;
        bool autoRecenterEnabled_ = false;
        float autoRecenterDelaySeconds_ = 1.25f;
        float autoRecenterSpeedDegreesPerSecond_ = 90.0f;

        bool collisionEnabled_ = true;
        bool collisionStaticOnly_ = true;
        float collisionRadius_ = 0.25f;
        float collisionPadding_ = 0.08f;
        uint32_t collisionLayerMask_ = 0xFFFFFFFFu;

        // Normalized viewport anchors use a top-left origin.  These defaults
        // frame a two-subject duel around the thirds while leaving a generous
        // soft region for ordinary locomotion.
        bool screenCompositionEnabled_ = true;
        float screenCompositionPrimaryAnchorX_ = 0.38f;
        float screenCompositionPrimaryAnchorY_ = 0.58f;
        float screenCompositionSecondaryAnchorX_ = 0.62f;
        float screenCompositionSecondaryAnchorY_ = 0.46f;
        float screenCompositionSoftZoneHalfWidth_ = 0.075f;
        float screenCompositionSoftZoneHalfHeight_ = 0.10f;
        float screenCompositionHardZoneHalfWidth_ = 0.20f;
        float screenCompositionHardZoneHalfHeight_ = 0.24f;
        float screenCompositionPreferredShoulderAngleDegrees_ = 32.0f;
        float screenCompositionMaximumShoulderAngleDegrees_ = 58.0f;
        float screenCompositionFocusHeightBias_ = 0.22f;
        float screenCompositionHardResponseScale_ = 0.45f;
        float screenCompositionCollisionCenteringStrength_ = 0.72f;

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
