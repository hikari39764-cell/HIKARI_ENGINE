#include "Editor/Panels/HIKARI_InputActionMapPanel.h"

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
        "Input Actions are edited in a dedicated large workspace so scene "
        "authoring remains compact and readable.");
    ImGui::Spacing();

    if (ImGui::BeginTable(
            "InputProjectSummary", 3,
            ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Actions");
        ImGui::Text("%zu", map.actions.size());
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Contexts");
        ImGui::Text("%zu", map.contexts.size());
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Status");
        if (issues.empty() && conflicts.empty()) {
            ImGui::TextColored(
                ImVec4(0.42f, 0.9f, 0.5f, 1.0f), "Ready");
        } else {
            ImGui::TextColored(
                ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
                "%zu issue(s)", issues.size() + conflicts.size());
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Active Contexts");
    const std::vector<std::string>& activeContexts =
        inputService.Contexts().GetActiveContexts();
    if (activeContexts.empty()) {
        ImGui::TextDisabled("None");
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
                ? "Open Input Editor  *"
                : "Open Input Editor...",
            ImVec2(buttonWidth, 38.0f))) {
        openRequested_ = true;
    }
    if (editor_.IsDirty()) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
            "Unsaved input changes are kept in memory.");
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
                "Unsaved changes");
            ImGui::SameLine();
        } else {
            ImGui::TextDisabled("All changes saved");
            ImGui::SameLine();
        }
        const float closeWidth = 110.0f;
        ImGui::SetCursorPosX(
            (std::max)(ImGui::GetCursorPosX(),
                ImGui::GetWindowContentRegionMax().x - closeWidth));
        if (ImGui::Button("Close", ImVec2(closeWidth, 0.0f))) {
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
