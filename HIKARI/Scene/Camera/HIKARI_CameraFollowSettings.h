#pragma once
#include "Core/Math/HIKARI_Math3D.h"
#include <cstdint>
#include <json.hpp>
#include <string>

namespace HIKARI::CAMERA {
// 再利用可能な設定。シーン参照と実行中の状態はコンポーネントが所有する。
struct CameraFollowSettings {
    MATH::Vec3 pivotOffset{0.0f, 1.2f, 0.0f};
    float initialYawDegrees = 0.0f;
    float initialPitchDegrees = 28.0f;
    float initialDistance = 8.0f;
    float minimumPitchDegrees = -65.0f;
    float maximumPitchDegrees = 75.0f;
    float minimumDistance = 1.0f;
    float maximumDistance = 20.0f;
    float followSmooth = 12.0f;
    float lookSmooth = 14.0f;
    float distanceSmooth = 18.0f;

    bool orbitInputEnabled = true;
    std::string lookActionId{"Gameplay.Look"};
    std::string zoomActionId{"Gameplay.CameraZoom"};
    std::string recenterActionId{"Gameplay.CameraRecenter"};
    float yawSpeedDegreesPerSecond = 180.0f;
    float pitchSpeedDegreesPerSecond = 140.0f;
    float mouseSensitivityDegreesPerPixel = 0.12f;
    float zoomSpeedUnitsPerSecond = 30.0f;
    float mouseWheelZoomUnitsPerStep = 1.0f;
    bool invertVerticalLook = false;
    bool autoRecenterEnabled = false;
    float autoRecenterDelaySeconds = 1.25f;
    float autoRecenterSpeedDegreesPerSecond = 90.0f;

    bool collisionEnabled = true;
    bool collisionStaticOnly = true;
    float collisionRadius = 0.25f;
    float collisionPadding = 0.08f;
    uint32_t collisionLayerMask = 0xFFFFFFFFu;

    // Normalized viewport anchors use a top-left origin.  These defaults
    // frame a two-subject duel around the thirds while leaving a generous
    // soft region for ordinary locomotion.
    bool screenCompositionEnabled = true;
    float screenCompositionPrimaryAnchorX = 0.38f;
    float screenCompositionPrimaryAnchorY = 0.58f;
    float screenCompositionSecondaryAnchorX = 0.62f;
    float screenCompositionSecondaryAnchorY = 0.46f;
    float screenCompositionSoftZoneHalfWidth = 0.075f;
    float screenCompositionSoftZoneHalfHeight = 0.10f;
    float screenCompositionHardZoneHalfWidth = 0.20f;
    float screenCompositionHardZoneHalfHeight = 0.24f;
    float screenCompositionPreferredShoulderAngleDegrees = 32.0f;
    float screenCompositionMaximumShoulderAngleDegrees = 58.0f;
    float screenCompositionFocusHeightBias = 0.22f;
    float screenCompositionHardResponseScale = 0.45f;
    float screenCompositionCollisionCenteringStrength = 0.72f;

    void Serialize(nlohmann::json& out) const;
    void Deserialize(const nlohmann::json& in);
    void Normalize() noexcept;
};
} // namespace HIKARI::CAMERA
