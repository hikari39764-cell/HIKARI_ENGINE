#include "Editor/Panels/HIKARI_InputActionMapPanel.h"

#include "Core/Text/HIKARI_Localization.h"
#include <algorithm>

#include "Input/Assets/HIKARI_InputActionMap.h"
#include "Input/Runtime/HIKARI_InputService.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

void InputActionMapPanel::RequestOpen() noexcept {
    openRequested_ = true;
}

void InputActionMapPanel::DrawLauncher(INPUT::InputService& inputService) {
#if defined(HIKARI_WITH_EDITOR)
    const INPUT::InputActionMap& map = inputService.GetActionMap();
    const std::vector<std::string> issues = map.Validate();
    const std::vector<INPUT::InputBindingConflict> conflicts =
        map.FindConflicts();

    ImGui::TextWrapped(
        "%s",
        TEXT::Localize("ui.input.launcherHint").c_str());
    ImGui::Spacing();

    if (ImGui::BeginTable(
            "InputProjectSummary", 3,
            ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        ImGui::TextDisabled(
            "%s",
            TEXT::Localize("ui.input.actions").c_str());
        ImGui::Text("%zu", map.actions.size());
        ImGui::TableNextColumn();
        ImGui::TextDisabled(
            "%s",
            TEXT::Localize("ui.input.contexts").c_str());
        ImGui::Text("%zu", map.contexts.size());
        ImGui::TableNextColumn();
        ImGui::TextDisabled(
            "%s",
            TEXT::Localize("ui.input.status").c_str());
        if (issues.empty() && conflicts.empty()) {
            ImGui::TextColored(
                ImVec4(0.42f, 0.9f, 0.5f, 1.0f),
                "%s",
                TEXT::Localize("ui.input.ready").c_str());
        } else {
            ImGui::TextColored(
                ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
                TEXT::Localize("ui.input.issuesFormat").c_str(),
                issues.size() + conflicts.size());
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextDisabled(
        "%s",
        TEXT::Localize("ui.input.activeContexts").c_str());
    const std::vector<std::string>& activeContexts =
        inputService.Contexts().GetActiveContexts();
    if (activeContexts.empty()) {
        ImGui::TextDisabled(
            "%s",
            TEXT::Localize("ui.input.none").c_str());
    } else {
        for (size_t index = 0; index < activeContexts.size(); ++index) {
            if (index > 0) ImGui::SameLine();
            ImGui::TextColored(
                ImVec4(0.35f, 0.78f, 0.92f, 1.0f),
                "%s", activeContexts[index].c_str());
        }
    }

    ImGui::Spacing();
    const float buttonWidth = (std::min)(
        320.0f, ImGui::GetContentRegionAvail().x);
    if (ImGui::Button(
            editor_.IsDirty()
                ? TEXT::Localize("ui.input.openEditorDirty").c_str()
                : TEXT::Localize("ui.input.openEditor").c_str(),
            ImVec2(buttonWidth, 38.0f))) {
        openRequested_ = true;
    }
    if (editor_.IsDirty()) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
            "%s",
            TEXT::Localize("ui.input.unsavedInMemory").c_str());
    }
    ImGui::TextDisabled(
        "%s", inputService.GetInputDirectory().generic_string().c_str());
#else
    (void)inputService;
#endif
}

void InputActionMapPanel::DrawModal(INPUT::InputService& inputService) {
#if defined(HIKARI_WITH_EDITOR)
    constexpr const char* kPopupId =
        "Project Input Editor###HikariProjectInputEditor";
    if (openRequested_) {
        ImGui::OpenPopup(kPopupId);
        openRequested_ = false;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 modalSize{
        (std::min)(1180.0f, viewport->WorkSize.x * 0.92f),
        (std::min)(780.0f, viewport->WorkSize.y * 0.90f) };
    ImGui::SetNextWindowPos(
        viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(modalSize, ImGuiCond_Appearing);

    bool keepOpen = true;
    if (ImGui::BeginPopupModal(
            kPopupId,
            &keepOpen,
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings)) {
        const float footerHeight =
            ImGui::GetFrameHeightWithSpacing() + 6.0f;
        if (ImGui::BeginChild(
                "##InputEditorBody",
                ImVec2(0.0f, -footerHeight),
                false,
                ImGuiWindowFlags_NoScrollbar)) {
            editor_.Draw(inputService);
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (editor_.IsDirty()) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
                "%s",
                TEXT::Localize("ui.input.unsavedChanges").c_str());
            ImGui::SameLine();
        } else {
            ImGui::TextDisabled(
                "%s",
                TEXT::Localize("ui.input.allChangesSaved").c_str());
            ImGui::SameLine();
        }
        const float closeWidth = 110.0f;
        ImGui::SetCursorPosX(
            (std::max)(ImGui::GetCursorPosX(),
                ImGui::GetWindowContentRegionMax().x - closeWidth));
        if (ImGui::Button(
                TEXT::Localize("ui.input.close").c_str(),
                ImVec2(closeWidth, 0.0f))) {
            keepOpen = false;
        }
        if (!keepOpen) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
#else
    (void)inputService;
#endif
}

} // namespace HIKARI
