#pragma once

#include <cstdint>

#include "Core/Math/HIKARI_MathValidation.h"

namespace HIKARI {

// 筆触の殻（Painterly Shell）の設定。形状（層数・厚み・LOD）と筆触の見え方（マスクの UV・色差・法線・被覆）は
// 材質の uniform 値から決まり、描画ごとの layer は renderer が所有する。
struct MaterialShellSettings {
    uint32_t maxLayers = 0u;
    float thickness = 0.015f;
    float lodStartPixels = 24.0f;
    float lodFullPixels = 160.0f;
    // 筆触マスクの UV 繰り返し数と回転 [rad]。層ごとの UV 段差は renderer が加える。
    float strokeTiling = 1.0f;
    float strokeRotation = 0.0f;
    // 筆触による明暗の揺らぎと筆触法線の強さ。基底面と殻の両方に掛かる。
    float strokeVariation = 0.25f;
    float strokeNormalStrength = 0.65f;
    // 殻の被覆。輪郭への集中（1 - N·V の冪）と被覆率。
    float edgePower = 1.5f;
    float coverage = 0.72f;

    bool operator==(const MaterialShellSettings&) const noexcept = default;
};

inline constexpr uint32_t kMaterialShellMaximumLayers = 8u;
// 殻の押し出しの揺らぎ（筆の圧）。厚みは 1 ± この値の範囲で変わる。HLSL の HIKARI_SHELL_PUSH_VARIATION と同じ。
inline constexpr float kMaterialShellPushVariation = 0.45f;

inline bool IsValidMaterialShellSettings(const MaterialShellSettings& settings) noexcept {
    const bool layersValid = settings.maxLayers == 0u || settings.maxLayers == 2u || settings.maxLayers == 4u ||
                             settings.maxLayers == kMaterialShellMaximumLayers;
    const bool strokeValid = MATH::IsFinite(settings.strokeTiling) && settings.strokeTiling > 0.0f &&
                             settings.strokeTiling <= 64.0f && MATH::IsFinite(settings.strokeRotation) &&
                             MATH::IsFinite(settings.strokeVariation) && settings.strokeVariation >= 0.0f &&
                             settings.strokeVariation <= 1.0f && MATH::IsFinite(settings.strokeNormalStrength) &&
                             settings.strokeNormalStrength >= 0.0f && settings.strokeNormalStrength <= 4.0f &&
                             MATH::IsFinite(settings.edgePower) && settings.edgePower > 0.0f &&
                             settings.edgePower <= 16.0f && MATH::IsFinite(settings.coverage) &&
                             settings.coverage >= 0.0f && settings.coverage <= 1.0f;
    return layersValid && strokeValid && MATH::IsFinite(settings.thickness) && settings.thickness >= 0.0f &&
           settings.thickness <= 1.0f && MATH::IsFinite(settings.lodStartPixels) && settings.lodStartPixels >= 0.0f &&
           MATH::IsFinite(settings.lodFullPixels) && settings.lodFullPixels > settings.lodStartPixels &&
           settings.lodFullPixels <= 16384.0f;
}

} // namespace HIKARI
