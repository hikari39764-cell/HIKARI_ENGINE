#pragma once

namespace HIKARI::CAMERA {

inline constexpr float kMinDepthOfFieldFocusDistance = 0.01f;
inline constexpr float kMinDepthOfFieldTransition = 0.01f;
inline constexpr float kMaxDepthOfFieldBlurRadius1080pPixels = 32.0f;

struct DepthOfFieldSettings {
    bool enabled = false;
    // 焦点距離と範囲・遷移はカメラ前方（view-Z）のワールド単位。
    float focusDistance = 10.0f;
    float focusRange = 2.0f;
    float nearTransition = 5.0f;
    float farTransition = 20.0f;
    // 1080p 出力高を基準にした半径。内部解像度への換算は renderer が行う。
    float nearBlurRadius1080pPixels = 12.0f;
    float farBlurRadius1080pPixels = 4.0f;

    friend constexpr bool operator==(const DepthOfFieldSettings&, const DepthOfFieldSettings&) = default;
};

DepthOfFieldSettings NormalizeDepthOfFieldSettings(DepthOfFieldSettings settings) noexcept;
bool IsValidDepthOfFieldSettings(const DepthOfFieldSettings& settings) noexcept;

} // namespace HIKARI::CAMERA
