#pragma once

#include <string>

#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI {
class AssetDatabase;
class AssetRegistry;
} // namespace HIKARI

namespace HIKARI::EDITOR::ENVIRONMENT_PANEL {

#if defined(HIKARI_WITH_EDITOR)
void NormalizeDirectionalLight(DirectionalLight &light);

bool DrawSkyAssetPicker(const AssetRegistry *assetRegistry,
                        const AssetDatabase *assetDatabase, std::string &value);

bool DrawReflectionProbeAssetPicker(const AssetRegistry *assetRegistry,
                                    const AssetDatabase *assetDatabase,
                                    std::string &value);

void ApplyEnvironmentPreset(SceneEnvironment &environment, int presetIndex);
#endif

} // namespace HIKARI::EDITOR::ENVIRONMENT_PANEL
