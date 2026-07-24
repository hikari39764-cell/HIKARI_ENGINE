#include "Editor/Panels/HIKARI_InputActionMapEditor.h"

#include <algorithm>

#include "Editor/Panels/HIKARI_InputActionMapEditorWidgets.h"
#include "Input/Runtime/HIKARI_InputService.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

void InputActionMapEditor::DrawActionsTab(
    INPUT::InputService& inputService) {
#if defined(HIKARI_WITH_EDITOR)
    using namespace EDITOR::INPUT_WIDGETS;
    INPUT::InputActionMap& map = inputService.EditActionMap();
    const float listWidth = (std::min)(340.0f,
        ImGui::GetContentRegionAvail().x * 0.38f);
    if (ImGui::BeginChild("##ActionBrowser", ImVec2(listWidth, 0.0f), true)) {
        if (ImGui::Button("+ New Action", ImVec2(118.0f, 0.0f))) {
            addActionRequested_ = true;
        }
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint(
            "##ActionFilter", "Filter actions...",
            actionFilter_.data(), actionFilter_.size());
        ImGui::Separator();
        for (int index = 0; index < static_cast<int>(map.actions.size()); ++index) {
            const INPUT::InputActionDefinition& action = map.actions[index];
            if (!MatchesFilter(action.actionId, actionFilter_.data()) &&
                !MatchesFilter(action.displayName, actionFilter_.data())) {
                continue;
            }
            ImGui::PushID(index);
            if (ImGui::Selectable(
                    action.displayName.c_str(), selectedAction_ == index)) {
                selectedAction_ = index;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", INPUT::ToString(action.valueType));
            ImGui::TextDisabled("%s", action.actionId.c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    if (ImGui::BeginChild("##ActionDetails", ImVec2(0.0f, 0.0f), true)) {
        if (map.actions.empty()) {
            ImGui::TextDisabled("Create an action to begin.");
        } else {
            INPUT::InputActionDefinition& action = map.actions[selectedAction_];
            ImGui::TextDisabled("ACTION DETAILS");
            ImGui::Separator();
            ImGui::TextDisabled("Stable ID");
            ImGui::TextUnformatted(action.actionId.c_str());
            ImGui::Spacing();
            ImGui::SetNextItemWidth(360.0f);
            if (EditString("Display Name", action.displayName)) dirty_ = true;
            ImGui::SetNextItemWidth(220.0f);
            if (DrawValueTypeCombo("Value Type", action.valueType)) dirty_ = true;
            if (ImGui::Checkbox("Clamp / Normalize Values", &action.clampValue)) {
                dirty_ = true;
            }
            ImGui::TextWrapped(
                "The stable ID is used by components and gameplay code. "
                "Display Name and value processing can be changed safely.");

            size_t bindingCount = 0;
            for (const INPUT::InputContextDefinition& context : map.contexts) {
                bindingCount += static_cast<size_t>(std::count_if(
                    context.bindings.begin(), context.bindings.end(),
                    [&action](const INPUT::InputBinding& binding) {
                        return binding.actionId == action.actionId;
                    }));
            }
            ImGui::Spacing();
            ImGui::Text("Bindings: %zu", bindingCount);
            ImGui::Spacing();
            if (ImGui::Button("Delete Action...")) {
                deleteActionRequested_ = true;
            }
        }
    }
    ImGui::EndChild();
#else
    (void)inputService;
#endif
}

void InputActionMapEditor::DrawActionPopups(
    INPUT::InputService& inputService) {
#if defined(HIKARI_WITH_EDITOR)
    using namespace EDITOR::INPUT_WIDGETS;
    INPUT::InputActionMap& map = inputService.EditActionMap();
    if (addActionRequested_) {
        ImGui::OpenPopup("Add Input Action");
        addActionRequested_ = false;
    }
    if (ImGui::BeginPopupModal(
            "Add Input Action", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "Create a stable action contract. Bind physical controls in a context afterwards.");
        ImGui::SetNextItemWidth(340.0f);
        ImGui::InputText("Action ID", newActionId_.data(), newActionId_.size());
        ImGui::SetNextItemWidth(340.0f);
        ImGui::InputText(
            "Display Name", newActionName_.data(), newActionName_.size());
        INPUT::InputActionValueType type =
            static_cast<INPUT::InputActionValueType>(newActionType_);
        ImGui::SetNextItemWidth(220.0f);
        (void)DrawValueTypeCombo("Value Type", type);
        newActionType_ = static_cast<int>(type);
        const bool canAdd = newActionId_[0] != '\0' &&
            map.FindAction(newActionId_.data()) == nullptr;
        ImGui::BeginDisabled(!canAdd);
        if (ImGui::Button("Create Action", ImVec2(140.0f, 0.0f))) {
            map.actions.push_back(INPUT::InputActionDefinition{
                newActionId_.data(),
                newActionName_[0] != '\0'
                    ? newActionName_.data() : newActionId_.data(),
                type,
                true });
            selectedAction_ = static_cast<int>(map.actions.size()) - 1;
            newActionId_.fill('\0');
            newActionName_.fill('\0');
            dirty_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (deleteActionRequested_) {
        ImGui::OpenPopup("Delete Input Action");
        deleteActionRequested_ = false;
    }
    if (ImGui::BeginPopupModal(
            "Delete Input Action", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!map.actions.empty()) {
            const std::string id = map.actions[selectedAction_].actionId;
            ImGui::TextWrapped(
                "Delete %s and every binding that references it?",
                id.c_str());
            if (ImGui::Button("Delete", ImVec2(120.0f, 0.0f))) {
                (void)map.RemoveAction(id);
                selectedAction_ = (std::max)(0, selectedAction_ - 1);
                dirty_ = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
#else
    (void)inputService;
#endif
}

} // namespace HIKARI
