#include "Editor/Panels/HIKARI_SceneSystemsPanel.h"

#include <utility>

#include "Editor/SystemAuthoring/HIKARI_SceneSystemAuthoringModel.h"
#include "Editor/SystemAuthoring/HIKARI_SystemAuthoringRegistry.h"
#include "Editor/Tools/HIKARI_EditorToolHost.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_SystemScheduler.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

    SceneSystemsPanelResult SceneSystemsPanel::Draw(
        DocumentSceneBase& scene,
        const EDITOR::SystemAuthoringRegistry& authoringRegistry,
        EDITOR::EditorToolHost& toolHost) {

        SceneSystemsPanelResult result{};
#if defined(HIKARI_WITH_EDITOR)
        SceneDocument& document = scene.GetSceneDocument();
        const std::vector<SceneSystemData> defaults =
            scene.CreateProjectDefaultSceneSystems();
        if (document.systems.empty()) {
            document.systems = defaults;
        }

        const std::vector<EDITOR::SceneSystemAuthoringRow> rows =
            EDITOR::BuildSceneSystemAuthoringRows(
                document,
                scene.GetSystemTypeRegistry(),
                scene.GetComponentSystemPolicy(),
                scene.GetSystemScheduler(),
                defaults);

        size_t installedCount = 0;
        size_t activeCount = 0;
        size_t automaticCount = 0;
        size_t issueCount = 0;
        for (const EDITOR::SceneSystemAuthoringRow& row : rows) {
            if (!EDITOR::IsSceneSystemInstalled(row)) {
                continue;
            }
            ++installedCount;
            automaticCount += static_cast<size_t>(
                EDITOR::IsSceneSystemAutomatic(row));
            activeCount += static_cast<size_t>(
                EDITOR::IsSceneSystemEffectivelyEnabled(row, document) &&
                row.scheduled);
            issueCount += static_cast<size_t>(
                EDITOR::HasSceneSystemIssue(row, document));
        }

        ImGui::Text("Installed %zu", installedCount);
        ImGui::SameLine();
        ImGui::TextDisabled("|  Active %zu", activeCount);
        ImGui::SameLine();
        ImGui::TextDisabled("|  Auto %zu", automaticCount);
        ImGui::SameLine();
        if (issueCount > 0) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
                "|  Issues %zu",
                issueCount);
        } else {
            ImGui::TextDisabled("|  Issues 0");
        }

        if (ImGui::Button(
                "Open System Manager...",
                ImVec2(-1.0f, 0.0f))) {
            manager_.Open();
        }
        if (runtimeApplyFailed_) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.4f, 0.35f, 1.0f),
                "Runtime update failed.");
        }

        EDITOR::SceneSystemsManagerResult managerResult =
            manager_.Draw(scene, authoringRegistry, toolHost);
        result.changed = managerResult.changed;
        result.label = std::move(managerResult.label);
        result.before = std::move(managerResult.before);
#else
        (void)scene;
        (void)authoringRegistry;
        (void)toolHost;
#endif
        return result;
    }

    void SceneSystemsPanel::SetRuntimeApplyStatus(bool success) {
        runtimeApplyFailed_ = !success;
    }

} // namespace HIKARI
