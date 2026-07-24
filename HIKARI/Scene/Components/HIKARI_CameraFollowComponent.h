#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "HIKARI_IComponent.h"
#include "Render3D/HIKARI_Math3D.h"
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
        float GetCollisionRadius() const noexcept;
        float GetCollisionPadding() const noexcept;
        uint32_t GetCollisionLayerMask() const noexcept {
            return collisionLayerMask_;
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
        float collisionRadius_ = 0.25f;
        float collisionPadding_ = 0.08f;
        uint32_t collisionLayerMask_ = 0xFFFFFFFFu;

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
    };

} // namespace HIKARI
