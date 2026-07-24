#pragma once

#include <array>
#include <string>
#include <vector>

#include "Editor/SystemAuthoring/HIKARI_AddSystemPopup.h"
#include "Editor/SystemAuthoring/HIKARI_SystemSettingsInspector.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {
    class DocumentSceneBase;
}

namespace HIKARI::EDITOR {

    class EditorToolHost;
    class SystemAuthoringRegistry;

    struct SceneSystemsManagerResult {
        bool changed = false;
        std::string label{};
        std::vector<SceneSystemData> before{};
    };

    class SceneSystemsManager {
    public:
        void Open();

        SceneSystemsManagerResult Draw(
            DocumentSceneBase& scene,
            const SystemAuthoringRegistry& authoringRegistry,
            EditorToolHost& toolHost);

    private:
        bool openRequested_ = false;
        bool discardPromptRequested_ = false;
        bool pendingClose_ = false;
        std::array<char, 128> filter_{};
        std::string selectedSystemId_{};
        std::string pendingSystemId_{};
        AddSystemPopup addSystemPopup_{};
        SystemSettingsInspector inspector_{};
    };

} // namespace HIKARI::EDITOR
