#pragma once

#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Render3D/Core/HIKARI_CameraOptics.h"

// 被写界深度とモーションブラーの項目表。値の正規化（HIKARI_CameraOptics.cpp）と、カメラ部件の保存・検査器・説明が
// 同じ表を使う。
namespace HIKARI::CAMERA {

// 焦点距離・範囲・遷移の上限（ワールド単位）。これを超える値は丸める。
inline constexpr double kMaxDepthOfFieldDistance = 100000.0;

inline constexpr auto kDepthOfFieldFields = [](auto&& field) {
    using S = DepthOfFieldSettings;
    using namespace JSONFIELDS;
    constexpr auto active = [](const S& settings) { return settings.enabled; };
    field("enabled", &S::enabled, Label{"Enabled"}, Help{"Apply depth of field for this camera."});
    field("focusDistance", &S::focusDistance, Range{kMinDepthOfFieldFocusDistance, kMaxDepthOfFieldDistance},
          Label{"Focus Distance"}, Help{"Camera forward-axis view-Z distance in world units."}, Show{active});
    field("focusRange", &S::focusRange, Range{0.0, kMaxDepthOfFieldDistance}, Label{"Focus Range"},
          Help{"Total clear-band width centered on Focus Distance, in world units."}, Show{active});
    field("nearTransition", &S::nearTransition, Range{kMinDepthOfFieldTransition, kMaxDepthOfFieldDistance},
          Label{"Near Transition"}, Help{"Near-side transition length along view-Z, in world units."}, Show{active});
    field("farTransition", &S::farTransition, Range{kMinDepthOfFieldTransition, kMaxDepthOfFieldDistance},
          Label{"Far Transition"}, Help{"Far-side transition length along view-Z, in world units."}, Show{active});
    field("nearBlurRadius1080pPixels", &S::nearBlurRadius1080pPixels, Range{0.0, kMaxDepthOfFieldBlurRadius1080pPixels},
          Label{"Near Blur Radius (1080p pixels)"}, Help{"Radius at 1080p output height; scaled with output height."},
          Show{active});
    field("farBlurRadius1080pPixels", &S::farBlurRadius1080pPixels, Range{0.0, kMaxDepthOfFieldBlurRadius1080pPixels},
          Label{"Far Blur Radius (1080p pixels)"}, Help{"Radius at 1080p output height; scaled with output height."},
          Show{active});
};

inline constexpr auto kMotionBlurFields = [](auto&& field) {
    using S = MotionBlurSettings;
    using namespace JSONFIELDS;
    constexpr auto active = [](const S& settings) { return settings.enabled; };
    field("enabled", &S::enabled, Label{"Enabled"},
          Help{"Blur moving objects and camera motion (needs TAA, DLAA or DLSS)."});
    field("shutterAngleDegrees", &S::shutterAngleDegrees, Slider(0.0, 360.0), Label{"Shutter Angle"},
          Help{"Degrees of the frame the shutter stays open; 180 is film."}, Show{active});
    field("maxBlur1080pPixels", &S::maxBlur1080pPixels, Range{0.0, kMaxMotionBlurBlur1080pPixels},
          Label{"Max Blur (1080p pixels)"}, Help{"Longest streak at 1080p output height."}, Show{active});
};

} // namespace HIKARI::CAMERA
