#pragma once

#include <string>
#include <vector>

#include "Core/Presentation/HIKARI_LogicalDisplayDefaults.h"
#include "Runtime/HIKARI_RuntimeHost.h"

namespace HIKARI::RUNTIME {

    struct HostBootstrapConfig {
        RuntimeHostMode hostMode = RuntimeHostMode::Game;
        bool enableDebugCamera = false;
        bool enableDebugLayer = true;
        bool resizableWindow = true;
        bool enableImGui = false;
        bool enableEditorUI = false;
        bool enableStreamline = true;
        int windowWidth = PRESENTATION::kDefaultLogicalWidth;
        int windowHeight = PRESENTATION::kDefaultLogicalHeight;
        std::string startupSceneGuid{};
        std::vector<std::string> exportedSceneGuids{};
    };

} // namespace HIKARI::RUNTIME
