#pragma once

#include <string>
#include <vector>

#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

    struct SceneObjectAuthoringHistoryRequest {
        std::string label{};
        std::vector<SceneObjectData> beforeObjects{};
        std::vector<SceneObjectData> afterObjects{};
        SceneCameraSettings beforeCamera{};
        SceneCameraSettings afterCamera{};
        bool dirtyBefore = false;
        bool runtimeWorldAffected = true;
    };

} // namespace HIKARI
