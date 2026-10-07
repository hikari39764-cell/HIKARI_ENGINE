#pragma once

#include <cstdint>

#include "Core/Math/HIKARI_MathValidation.h"

namespace HIKARI {

// Shell の形状は材質の uniform 値から決まり、描画ごとの layer は renderer が所有する。
struct MaterialShellSettings {
    uint32_t maxLayers = 0u;
    float thickness = 0.015f;
    float lodStartPixels = 24.0f;
    float lodFullPixels = 160.0f;

    bool operator==(const MaterialShellSettings&) const noexcept = default;
};

inline constexpr uint32_t kMaterialShellMaximumLayers = 8u;

inline bool IsValidMaterialShellSettings(const MaterialShellSettings& settings) noexcept {
    const bool layersValid = settings.maxLayers == 0u || settings.maxLayers == 2u || settings.maxLayers == 4u ||
                             settings.maxLayers == kMaterialShellMaximumLayers;
    return layersValid && MATH::IsFinite(settings.thickness) && settings.thickness >= 0.0f &&
           settings.thickness <= 1.0f && MATH::IsFinite(settings.lodStartPixels) && settings.lodStartPixels >= 0.0f &&
           MATH::IsFinite(settings.lodFullPixels) && settings.lodFullPixels > settings.lodStartPixels &&
           settings.lodFullPixels <= 16384.0f;
}

} // namespace HIKARI
