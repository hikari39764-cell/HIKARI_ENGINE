#pragma once

#include "RenderContracts/Material/HIKARI_MaterialShellSettings.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"

namespace HIKARI {

// Pin 名と cooked descriptor のキーを共有し、設定の投影と JSON を同じ表から処理する。
inline constexpr auto kMaterialShellFields = [](auto&& field) {
    field("MaxLayers", &MaterialShellSettings::maxLayers, JSONFIELDS::Range{0.0, kMaterialShellMaximumLayers});
    field("Thickness", &MaterialShellSettings::thickness, JSONFIELDS::Range{0.0, 1.0});
    field("LodStartPixels", &MaterialShellSettings::lodStartPixels, JSONFIELDS::Range{0.0, 16384.0});
    field("LodFullPixels", &MaterialShellSettings::lodFullPixels, JSONFIELDS::Range{0.0, 16384.0});
    field("StrokeTiling", &MaterialShellSettings::strokeTiling, JSONFIELDS::Range{0.01, 64.0});
    field("StrokeRotation", &MaterialShellSettings::strokeRotation, JSONFIELDS::Range{-6.3, 6.3});
    field("StrokeVariation", &MaterialShellSettings::strokeVariation, JSONFIELDS::Range{0.0, 1.0});
    field("StrokeNormalStrength", &MaterialShellSettings::strokeNormalStrength, JSONFIELDS::Range{0.0, 4.0});
    field("EdgePower", &MaterialShellSettings::edgePower, JSONFIELDS::Range{0.1, 16.0});
    field("Coverage", &MaterialShellSettings::coverage, JSONFIELDS::Range{0.0, 1.0});
};

} // namespace HIKARI
