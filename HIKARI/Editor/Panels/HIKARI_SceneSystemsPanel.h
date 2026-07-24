#pragma once

#include <string>
#include <vector>

#include "Editor/SystemAuthoring/HIKARI_SceneSystemsManager.h"

namespace HIKARI {
    class DocumentSceneBase;
}

namespace HIKARI::EDITOR {
    class EditorToolHost;
    class SystemAuthoringRegistry;
}

namespace HIKARI {

    struct SceneSystemsPanelResult {
        bool changed = false;
        std::string label{};
        std::vector<SceneSystemData> before{};
    };

    class SceneSystemsPanel {
    public:
        SceneSystemsPanelResult Draw(
            DocumentSceneBase& scene,
            const EDITOR::SystemAuthoringRegistry& authoringRegistry,
            EDITOR::EditorToolHost& toolHost);

        void SetRuntimeApplyStatus(bool success);

    private:
        EDITOR::SceneSystemsManager manager_{};
        bool runtimeApplyFailed_ = false;
    };

} // namespace HIKARI
