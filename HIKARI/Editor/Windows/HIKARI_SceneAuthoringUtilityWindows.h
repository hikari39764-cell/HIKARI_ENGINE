#pragma once

#include "Editor/Panels/HIKARI_InputActionMapPanel.h"
#include "Editor/Panels/HIKARI_ProjectFeaturesPanel.h"
#include "Editor/Panels/HIKARI_SceneSystemsPanel.h"

namespace HIKARI {

    struct AuthoringWindowState;
    class DocumentSceneBase;

    namespace INPUT {
        class InputService;
    }

    namespace EDITOR {
        class EditorToolHost;
        class SystemAuthoringRegistry;

        // Owns scene-authoring utilities that need focused, non-dockable
        // surfaces. The scene hierarchy remains a hierarchy instead of
        // becoming a container for unrelated project and system editors.
        class SceneAuthoringUtilityWindows {
        public:
            SceneSystemsPanelResult Draw(
                DocumentSceneBase& scene,
                AuthoringWindowState& windowState,
                INPUT::InputService& inputService,
                const SystemAuthoringRegistry& systemRegistry,
                EditorToolHost& toolHost);

            void SetSystemsRuntimeApplyStatus(bool success);

        private:
            ProjectFeaturesPanel projectFeaturesPanel_{};
            SceneSystemsPanel sceneSystemsPanel_{};
            InputActionMapPanel inputActionMapPanel_{};
        };

    } // namespace EDITOR
} // namespace HIKARI
