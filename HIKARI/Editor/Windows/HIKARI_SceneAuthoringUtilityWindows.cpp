#include "Editor/Windows/HIKARI_SceneAuthoringUtilityWindows.h"

#include "Editor/HIKARI_EditorContext.h"
#include "Editor/SystemAuthoring/HIKARI_SystemAuthoringRegistry.h"
#include "Editor/Tools/HIKARI_EditorToolHost.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    SceneSystemsPanelResult SceneAuthoringUtilityWindows::Draw(
        DocumentSceneBase& scene,
        AuthoringWindowState& windowState,
        INPUT::InputService& inputService,
        const SystemAuthoringRegistry& systemRegistry,
        EditorToolHost& toolHost) {

        SceneSystemsPanelResult systemsResult{};
#if defined(HIKARI_WITH_EDITOR)
        constexpr ImGuiWindowFlags utilityFlags =
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoCollapse;

        if (windowState.showProjectFeatures) {
            ImGui::SetNextWindowSize(
                ImVec2(620.0f, 520.0f),
                ImGuiCond_FirstUseEver);
            if (ImGui::Begin(
                    "Project Features",
                    &windowState.showProjectFeatures,
                    utilityFlags)) {
                projectFeaturesPanel_.Draw(scene);
            }
            ImGui::End();
        }

        if (windowState.showSceneSystems) {
            ImGui::SetNextWindowSize(
                ImVec2(680.0f, 620.0f),
                ImGuiCond_FirstUseEver);
            if (ImGui::Begin(
                    "Scene Systems",
                    &windowState.showSceneSystems,
                    utilityFlags)) {
                systemsResult = sceneSystemsPanel_.Draw(
                    scene,
                    systemRegistry,
                    toolHost);
            }
            ImGui::End();
        }

        if (windowState.openInputEditorRequested) {
            inputActionMapPanel_.RequestOpen();
            windowState.openInputEditorRequested = false;
        }
        inputActionMapPanel_.DrawModal(inputService);
#else
        (void)scene;
        (void)windowState;
        (void)inputService;
        (void)systemRegistry;
        (void)toolHost;
#endif
        return systemsResult;
    }

    void SceneAuthoringUtilityWindows::SetSystemsRuntimeApplyStatus(
        bool success) {
        sceneSystemsPanel_.SetRuntimeApplyStatus(success);
    }

} // namespace HIKARI::EDITOR
