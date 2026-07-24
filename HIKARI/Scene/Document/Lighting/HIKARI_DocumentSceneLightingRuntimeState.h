#pragma once

#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_SkyManager.h"

namespace HIKARI {

    struct DocumentSceneLightingRuntimeState {
        SkyManager sky{};
        SceneEnvironment environment{};
        bool environmentLightingEnabled = true;
    };

} // namespace HIKARI
