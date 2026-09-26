#pragma once

#include <string>

#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI {
class AssetDatabase;
class AssetRegistry;
} // namespace HIKARI

namespace HIKARI::EDITOR::ENVIRONMENT_PANEL {

#if defined(HIKARI_WITH_EDITOR)
void DrawEnvironmentLightingControls(SceneEnvironment& environment);

bool DrawSkyAssetPicker(const AssetRegistry* assetRegistry, const AssetDatabase* assetDatabase, std::string& value);

bool DrawPanoramaTextureAssetPicker(const AssetDatabase* assetDatabase, const char* labelKey, std::string& value);

void ApplyEnvironmentPreset(SceneEnvironment& environment, int presetIndex);

struct EnvironmentPreset {
    const char* id;
    const char* labelKey;
};

// 表の順序が ApplyEnvironmentPreset の番号で、パネルのボタンの並びでもある。
inline constexpr EnvironmentPreset kEnvironmentPresets[] = {
    {"default", "ui.environment.preset.default"},        {"bright_day", "ui.environment.preset.brightDay"},
    {"sunset", "ui.environment.preset.sunset"},          {"night", "ui.environment.preset.night"},
    {"overcast", "ui.environment.preset.overcast"},      {"stylized_blue", "ui.environment.preset.stylizedBlue"},
    {"warm_indoor", "ui.environment.preset.warmIndoor"}, {"fresh_summer", "ui.environment.preset.freshSummer"},
    {"golden_hour", "ui.environment.preset.goldenHour"}, {"layered_panorama", "ui.environment.preset.layeredPanorama"},
};
#endif

} // namespace HIKARI::EDITOR::ENVIRONMENT_PANEL
