#include "Editor/Panels/HIKARI_InputActionMapEditor.h"

#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Panels/HIKARI_InputActionMapEditorWidgets.h"
#include "Input/Runtime/HIKARI_InputService.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

void InputActionMapEditor::DrawMonitorTab(
    INPUT::InputService& inputService) {
#if defined(HIKARI_WITH_EDITOR)
    using namespace EDITOR::INPUT_WIDGETS;
    const INPUT::InputActionMap& map = inputService.GetActionMap();
    const std::vector<std::string> validation = map.Validate();
    const std::vector<INPUT::InputBindingConflict> conflicts =
        map.FindConflicts();
    const INPUT::InputSnapshot& snapshot = inputService.GetSnapshot();

    if (validation.empty() && conflicts.empty()) {
        ImGui::TextColored(
            ImVec4(0.42f, 0.9f, 0.5f, 1.0f),
            "%s",
            TEXT::Localize("ui.input.projectValid").c_str());
    } else {
        ImGui::TextColored(
            ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
            TEXT::Localize(
                "ui.input.validationIssuesFormat").c_str(),
            validation.size() + conflicts.size());
    }
    ImGui::SameLine();
    ImGui::TextDisabled(
        TEXT::Localize("ui.input.frameDeviceFormat").c_str(),
        static_cast<unsigned long long>(snapshot.GetFrameIndex()),
        DeviceLabel(snapshot.GetLastActiveDevice()).c_str());

    if (!validation.empty() || !conflicts.empty()) {
        if (ImGui::BeginChild(
                "##InputValidation", ImVec2(0.0f, 120.0f), true)) {
            for (const std::string& issue : validation) {
                ImGui::BulletText(
                    TEXT::Localize("ui.input.errorFormat").c_str(),
                    issue.c_str());
            }
            for (const INPUT::InputBindingConflict& conflict : conflicts) {
                ImGui::BulletText(
                    TEXT::Localize(
                        "ui.input.conflictFormat").c_str(),
                    conflict.contextId.c_str(), conflict.control.c_str(),
                    conflict.firstActionId.c_str(),
                    conflict.secondActionId.c_str());
            }
        }
        ImGui::EndChild();
    }

    ImGui::Checkbox(
        TEXT::Localize("ui.input.onlyActiveActions").c_str(),
        &showOnlyActiveActions_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputTextWithHint(
        "##MonitorFilter",
        TEXT::Localize("ui.input.filterRuntime").c_str(),
        monitorFilter_.data(), monitorFilter_.size());
    if (ImGui::BeginTable(
            "InputRuntimeActions", 5,
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_Resizable,
            ImVec2(0.0f, -4.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(
            TEXT::Localize("ui.input.tableAction").c_str());
        ImGui::TableSetupColumn(
            TEXT::Localize("ui.input.tableType").c_str());
        ImGui::TableSetupColumn(
            TEXT::Localize("ui.input.tableValue").c_str());
        ImGui::TableSetupColumn(
            TEXT::Localize("ui.input.tableState").c_str());
        ImGui::TableSetupColumn(
            TEXT::Localize("ui.input.tableHeld").c_str());
        ImGui::TableHeadersRow();
        for (const INPUT::InputActionDefinition& action : map.actions) {
            if (!MatchesFilter(action.actionId, monitorFilter_.data()) &&
                !MatchesFilter(action.displayName, monitorFilter_.data())) {
                continue;
            }
            const INPUT::InputActionState* state =
                snapshot.FindAction(action.actionId);
            if (state == nullptr || (showOnlyActiveActions_ &&
                    !state->down && !state->pressed && !state->released)) {
                continue;
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(action.actionId.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", INPUT::ToString(action.valueType));
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.3f, %.3f", state->x, state->y);
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(
                state->pressed
                    ? TEXT::Localize("ui.input.statePressed").c_str()
                    : (state->released
                        ? TEXT::Localize("ui.input.stateReleased").c_str()
                        : (state->down
                            ? TEXT::Localize("ui.input.stateDown").c_str()
                            : TEXT::Localize("ui.input.stateIdle").c_str())));
            ImGui::TableSetColumnIndex(4);
            ImGui::Text(
                TEXT::Localize("ui.input.heldFormat").c_str(),
                state->heldSeconds);
        }
        ImGui::EndTable();
    }
#else
    (void)inputService;
#endif
}

} // namespace HIKARI
