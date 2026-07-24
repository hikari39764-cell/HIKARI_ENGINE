#include "Editor/Panels/HIKARI_InputActionMapEditor.h"

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
            "Input project is valid");
    } else {
        ImGui::TextColored(
            ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
            "%zu validation issue(s)",
            validation.size() + conflicts.size());
    }
    ImGui::SameLine();
    ImGui::TextDisabled(
        "Frame %llu  |  Device %s",
        static_cast<unsigned long long>(snapshot.GetFrameIndex()),
        DeviceLabel(snapshot.GetLastActiveDevice()));

    if (!validation.empty() || !conflicts.empty()) {
        if (ImGui::BeginChild(
                "##InputValidation", ImVec2(0.0f, 120.0f), true)) {
            for (const std::string& issue : validation) {
                ImGui::BulletText("Error: %s", issue.c_str());
            }
            for (const INPUT::InputBindingConflict& conflict : conflicts) {
                ImGui::BulletText(
                    "%s: %s is bound to both %s and %s",
                    conflict.contextId.c_str(), conflict.control.c_str(),
                    conflict.firstActionId.c_str(),
                    conflict.secondActionId.c_str());
            }
        }
        ImGui::EndChild();
    }

    ImGui::Checkbox("Only show active actions", &showOnlyActiveActions_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputTextWithHint(
        "##MonitorFilter", "Filter runtime actions...",
        monitorFilter_.data(), monitorFilter_.size());
    if (ImGui::BeginTable(
            "InputRuntimeActions", 5,
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_Resizable,
            ImVec2(0.0f, -4.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Action");
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("State");
        ImGui::TableSetupColumn("Held");
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
                state->pressed ? "Pressed" :
                (state->released ? "Released" :
                    (state->down ? "Down" : "Idle")));
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.2f s", state->heldSeconds);
        }
        ImGui::EndTable();
    }
#else
    (void)inputService;
#endif
}

} // namespace HIKARI
