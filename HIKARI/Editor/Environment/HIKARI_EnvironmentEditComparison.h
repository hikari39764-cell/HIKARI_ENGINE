#pragma once

#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI::EDITOR::ENVIRONMENT_EDIT {

bool AreSceneEnvironmentValuesEqual(const SceneEnvironment &lhs,
                                    const SceneEnvironment &rhs);

bool AreQualityEnvironmentValuesEqual(const SceneEnvironment &lhs,
                                      const SceneEnvironment &rhs);

int ToSsaoModeIndex(const AmbientOcclusionSettings &settings);
SsaoMode SsaoModeFromIndex(int index);

} // namespace HIKARI::EDITOR::ENVIRONMENT_EDIT
