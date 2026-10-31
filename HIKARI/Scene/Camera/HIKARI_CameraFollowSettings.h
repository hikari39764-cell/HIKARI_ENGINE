#pragma once
#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Input/Runtime/HIKARI_InputTypes.h"
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
    // 前瞻：移動の向きへ見る点をずらす距離 [m]（4 m/s で全量）。
    float lookAheadDistance = 0.0f;
    // 垂直死区 [m]：追う物の上下がこの幅の中なら高さを追わない（跳んでも画面が上下に揺れない）。
    float verticalDeadZone = 0.0f;

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

    // 範囲へ丸め（非有限値は既定値）、項目どうしの関係（最小 ≤ 最大、肩越しの角度、画面上の二つの狙いの間隔など）を保つ。
    void Normalize();
};

// 入力アクションの選択欄（項目表の Draw 用）。Type は選べるアクションの値の型。
template <INPUT::InputActionValueType Type>
struct InputActionPicker {
    template <class Builder, class Field, class Owner>
    bool operator()(Builder& builder, const Field& field, std::string& value, const Owner&) const {
        return builder.InputActionIdPicker(field, Type, value);
    }
};

// 保存（プロファイルの follow と、組件の JSON）・読み取り・検査器・説明が共有する項目表。JSON のキー lookAction /
// zoomAction / recenterAction は lookActionId / zoomActionId / recenterActionId に対応する。
inline constexpr auto kCameraFollowFields = [](auto&& field) {
    using S = CameraFollowSettings;
    using namespace JSONFIELDS;
    constexpr auto orbiting = [](const S& s) { return s.orbitInputEnabled; };
    constexpr auto recentering = [](const S& s) { return s.autoRecenterEnabled; };
    constexpr auto colliding = [](const S& s) { return s.collisionEnabled; };
    constexpr auto composing = [](const S& s) { return s.screenCompositionEnabled; };

    field("pivotOffset", &S::pivotOffset, Range{-1000.0, 1000.0}, Label{"Pivot Offset"}, Unit{"m"},
          Help{"Point relative to the follow target that the camera orbits and looks at."}, Group{"Orbit"});
    field("initialYawDegrees", &S::initialYawDegrees, Range{-360.0, 360.0}, Label{"Initial Yaw"}, Group{"Orbit"});
    field("initialPitchDegrees", &S::initialPitchDegrees, Range{-89.0, 89.0}, Label{"Initial Pitch"}, Group{"Orbit"});
    field("initialDistance", &S::initialDistance, Range{0.01, 100000.0}, Label{"Initial Distance"}, Group{"Orbit"});
    field("minimumPitchDegrees", &S::minimumPitchDegrees, Range{-89.0, 89.0}, Label{"Minimum Pitch"}, Group{"Orbit"});
    field("maximumPitchDegrees", &S::maximumPitchDegrees, Range{-89.0, 89.0}, Label{"Maximum Pitch"}, Group{"Orbit"});
    field("minimumDistance", &S::minimumDistance, Range{0.01, 100000.0}, Label{"Minimum Distance"}, Group{"Orbit"});
    field("maximumDistance", &S::maximumDistance, Range{0.01, 100000.0}, Label{"Maximum Distance"}, Group{"Orbit"});

    field("followSmooth", &S::followSmooth, Range{0.0, 1000.0}, Label{"Follow Smooth"}, Group{"Smoothing"});
    field("lookSmooth", &S::lookSmooth, Range{0.0, 1000.0}, Label{"Look Smooth"}, Group{"Smoothing"});
    field("distanceSmooth", &S::distanceSmooth, Range{0.0, 1000.0}, Label{"Distance Smooth"}, Group{"Smoothing"});
    field("lookAheadDistance", &S::lookAheadDistance, Range{0.0, 20.0}, Label{"Look Ahead"}, Unit{"m"},
          Help{"Shifts the look point toward the movement direction by this distance (full amount at 4 m/s)."},
          Group{"Smoothing"});
    field("verticalDeadZone", &S::verticalDeadZone, Range{0.0, 10.0}, Label{"Vertical Dead Zone"}, Unit{"m"},
          Help{"Height changes within this band are not followed, so jumps do not bob the view."}, Group{"Smoothing"});

    field("orbitInputEnabled", &S::orbitInputEnabled, Label{"Enable Orbit Input"}, Group{"Input"});
    field("lookAction", &S::lookActionId, Label{"Look Action"},
          Draw{InputActionPicker<INPUT::InputActionValueType::Axis2D>{}}, Show{orbiting}, Group{"Input"});
    field("zoomAction", &S::zoomActionId, Label{"Zoom Action"},
          Draw{InputActionPicker<INPUT::InputActionValueType::Axis1D>{}}, Show{orbiting}, Group{"Input"});
    field("recenterAction", &S::recenterActionId, Label{"Recenter Action"},
          Draw{InputActionPicker<INPUT::InputActionValueType::Button>{}}, Show{orbiting}, Group{"Input"});
    field("yawSpeedDegreesPerSecond", &S::yawSpeedDegreesPerSecond, Range{0.0, 3600.0}, Label{"Gamepad Yaw Speed"},
          Unit{"deg/s"}, Show{orbiting}, Group{"Input"});
    field("pitchSpeedDegreesPerSecond", &S::pitchSpeedDegreesPerSecond, Range{0.0, 3600.0},
          Label{"Gamepad Pitch Speed"}, Unit{"deg/s"}, Show{orbiting}, Group{"Input"});
    field("mouseSensitivityDegreesPerPixel", &S::mouseSensitivityDegreesPerPixel, Range{0.0, 10.0},
          Label{"Mouse Sensitivity"}, Unit{"deg/pixel"}, Show{orbiting}, Group{"Input"});
    field("zoomSpeedUnitsPerSecond", &S::zoomSpeedUnitsPerSecond, Range{0.0, 1000.0}, Label{"Gamepad Zoom Speed"},
          Unit{"units/s"}, Show{orbiting}, Group{"Input"});
    field("mouseWheelZoomUnitsPerStep", &S::mouseWheelZoomUnitsPerStep, Range{0.0, 100.0},
          Label{"Mouse Wheel Zoom Step"}, Show{orbiting}, Group{"Input"});
    field("invertVerticalLook", &S::invertVerticalLook, Label{"Invert Vertical Look"}, Show{orbiting}, Group{"Input"});

    field("autoRecenterEnabled", &S::autoRecenterEnabled, Label{"Auto Recenter"}, Group{"Auto Recenter"});
    field("autoRecenterDelaySeconds", &S::autoRecenterDelaySeconds, Range{0.0, 3600.0}, Label{"Recenter Delay"},
          Show{recentering}, Group{"Auto Recenter"});
    field("autoRecenterSpeedDegreesPerSecond", &S::autoRecenterSpeedDegreesPerSecond, Range{0.0, 3600.0},
          Label{"Recenter Speed"}, Unit{"deg/s"}, Show{recentering}, Group{"Auto Recenter"});

    field("collisionEnabled", &S::collisionEnabled, Label{"Camera Collision"}, Group{"Collision"});
    field("collisionStaticOnly", &S::collisionStaticOnly, Label{"Camera Collision Static Only"}, Show{colliding},
          Group{"Collision"});
    field("collisionRadius", &S::collisionRadius, Range{0.01, 100.0}, Label{"Collision Radius"}, Show{colliding},
          Group{"Collision"});
    field("collisionPadding", &S::collisionPadding, Range{0.0, 100.0}, Label{"Collision Padding"}, Show{colliding},
          Group{"Collision"});
    field("collisionLayerMask", &S::collisionLayerMask, Label{"Collision Layer Mask"},
          Help{"Bit mask of the physics layers the camera collides with (-1 means all)."}, Show{colliding},
          Group{"Collision"});

    field("screenCompositionEnabled", &S::screenCompositionEnabled, Label{"Screen-space Target Composition"},
          Group{"Screen Composition"});
    field("screenCompositionPrimaryAnchorX", &S::screenCompositionPrimaryAnchorX, Range{0.05, 0.95},
          Label{"Primary Anchor X"}, Show{composing}, Group{"Screen Composition"});
    field("screenCompositionPrimaryAnchorY", &S::screenCompositionPrimaryAnchorY, Range{0.05, 0.95},
          Label{"Primary Anchor Y"}, Show{composing}, Group{"Screen Composition"});
    field("screenCompositionSecondaryAnchorX", &S::screenCompositionSecondaryAnchorX, Range{0.05, 0.95},
          Label{"Secondary Anchor X"}, Show{composing}, Group{"Screen Composition"});
    field("screenCompositionSecondaryAnchorY", &S::screenCompositionSecondaryAnchorY, Range{0.05, 0.95},
          Label{"Secondary Anchor Y"}, Show{composing}, Group{"Screen Composition"});
    field("screenCompositionSoftZoneHalfWidth", &S::screenCompositionSoftZoneHalfWidth, Range{0.0, 0.45},
          Label{"Soft Zone Half Width"}, Show{composing}, Group{"Screen Composition"});
    field("screenCompositionSoftZoneHalfHeight", &S::screenCompositionSoftZoneHalfHeight, Range{0.0, 0.45},
          Label{"Soft Zone Half Height"}, Show{composing}, Group{"Screen Composition"});
    field("screenCompositionHardZoneHalfWidth", &S::screenCompositionHardZoneHalfWidth, Range{0.01, 0.5},
          Label{"Hard Zone Half Width"}, Show{composing}, Group{"Screen Composition"});
    field("screenCompositionHardZoneHalfHeight", &S::screenCompositionHardZoneHalfHeight, Range{0.01, 0.5},
          Label{"Hard Zone Half Height"}, Show{composing}, Group{"Screen Composition"});
    field("screenCompositionPreferredShoulderAngleDegrees", &S::screenCompositionPreferredShoulderAngleDegrees,
          Range{0.0, 89.0}, Label{"Preferred Shoulder Angle"}, Show{composing}, Group{"Screen Composition"});
    field("screenCompositionMaximumShoulderAngleDegrees", &S::screenCompositionMaximumShoulderAngleDegrees,
          Range{0.0, 89.0}, Label{"Maximum Shoulder Angle"}, Show{composing}, Group{"Screen Composition"});
    field("screenCompositionFocusHeightBias", &S::screenCompositionFocusHeightBias, Range{-5.0, 5.0},
          Label{"Composition Focus Height Bias"}, Show{composing}, Group{"Screen Composition"});
    field("screenCompositionHardResponseScale", &S::screenCompositionHardResponseScale, Range{0.1, 1.0},
          Label{"Hard-zone Response Scale"}, Show{composing}, Group{"Screen Composition"});
    field("screenCompositionCollisionCenteringStrength", &S::screenCompositionCollisionCenteringStrength,
          Range{0.0, 1.0}, Label{"Collision Centering Strength"}, Show{composing}, Group{"Screen Composition"});
};
} // namespace HIKARI::CAMERA
