#include "Editor/Panels/HIKARI_SceneSystemsPanel.h"

#include "Core/Text/HIKARI_Localization.h"
#include <utility>

#include "Editor/SystemAuthoring/HIKARI_SceneSystemAuthoringModel.h"
#include "Editor/SystemAuthoring/HIKARI_SystemAuthoringRegistry.h"
#include "Editor/Tools/HIKARI_EditorToolHost.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_SystemScheduler.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#include "imgui.h"

namespace HIKARI {

SceneSystemsPanelResult SceneSystemsPanel::Draw(DocumentSceneBase& scene,
                                                const EDITOR::SystemAuthoringRegistry& authoringRegistry,
                                                EDITOR::EditorToolHost& toolHost) {
    SceneSystemsPanelResult result{};
    SceneDocument& document = scene.GetSceneDocument();
    const std::vector<SceneSystemData> defaults = scene.CreateProjectDefaultSceneSystems();
    if (document.systems.empty()) {
        document.systems = defaults;
    }

    const std::vector<EDITOR::SceneSystemAuthoringRow> rows =
        EDITOR::BuildSceneSystemAuthoringRows(document, scene.GetSystemTypeRegistry(), scene.GetComponentSystemPolicy(),
                                              scene.GetSystemScheduler(), defaults);

    size_t installedCount = 0;
    size_t activeCount = 0;
    size_t automaticCount = 0;
    size_t issueCount = 0;
    for (const EDITOR::SceneSystemAuthoringRow& row : rows) {
        if (!EDITOR::IsSceneSystemInstalled(row)) {
            continue;
        }
        ++installedCount;
        automaticCount += static_cast<size_t>(EDITOR::IsSceneSystemAutomatic(row));
        activeCount += static_cast<size_t>(EDITOR::IsSceneSystemEffectivelyEnabled(row, document) && row.scheduled);
        issueCount += static_cast<size_t>(EDITOR::HasSceneSystemIssue(row, document));
    }

    ImGui::Text(TEXT::Localize("ui.sceneSystems.installedFormat").c_str(), installedCount);
    ImGui::SameLine();
    ImGui::TextDisabled(TEXT::Localize("ui.sceneSystems.activeFormat").c_str(), activeCount);
    ImGui::SameLine();
    ImGui::TextDisabled(TEXT::Localize("ui.sceneSystems.autoFormat").c_str(), automaticCount);
    ImGui::SameLine();
    if (issueCount > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), TEXT::Localize("ui.sceneSystems.issuesFormat").c_str(),
                           issueCount);
    } else {
        ImGui::TextDisabled("%s", TEXT::Localize("ui.sceneSystems.issuesZero").c_str());
    }

    if (ImGui::Button(TEXT::Localize("ui.sceneSystems.openManager").c_str(), ImVec2(-1.0f, 0.0f))) {
        manager_.Open();
    }
    if (runtimeApplyFailed_) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "%s",
                           TEXT::Localize("ui.sceneSystems.runtimeUpdateFailed").c_str());
    }

    EDITOR::SceneSystemsManagerResult managerResult = manager_.Draw(scene, authoringRegistry, toolHost);
    result.changed = managerResult.changed;
    result.label = std::move(managerResult.label);
    result.before = std::move(managerResult.before);
    return result;
}

void SceneSystemsPanel::SetRuntimeApplyStatus(bool success) { runtimeApplyFailed_ = !success; }

} // namespace HIKARI
