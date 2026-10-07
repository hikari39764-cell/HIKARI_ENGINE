#pragma once

#include "Assets/Material/HIKARI_MaterialShellSettings.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"

namespace HIKARI {

// Pin 名と cooked descriptor のキーを共有し、設定の投影と JSON を同じ表から処理する。
inline constexpr auto kMaterialShellFields = [](auto&& field) {
    field("MaxLayers", &MaterialShellSettings::maxLayers, JSONFIELDS::Range{0.0, kMaterialShellMaximumLayers});
    field("Thickness", &MaterialShellSettings::thickness, JSONFIELDS::Range{0.0, 1.0});
    field("LodStartPixels", &MaterialShellSettings::lodStartPixels, JSONFIELDS::Range{0.0, 16384.0});
    field("LodFullPixels", &MaterialShellSettings::lodFullPixels, JSONFIELDS::Range{0.0, 16384.0});
};

} // namespace HIKARI
