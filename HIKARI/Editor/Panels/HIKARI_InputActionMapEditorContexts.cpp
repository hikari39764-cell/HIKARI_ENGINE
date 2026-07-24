#include "Editor/Panels/HIKARI_InputActionMapEditor.h"

#include <algorithm>
#include <utility>

#include "Editor/Panels/HIKARI_InputActionMapEditorWidgets.h"
#include "Input/Runtime/HIKARI_InputService.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

void InputActionMapEditor::DrawContextsTab(
    INPUT::InputService& inputService) {
#if defined(HIKARI_WITH_EDITOR)
    using namespace EDITOR::INPUT_WIDGETS;
    INPUT::InputActionMap& map = inputService.EditActionMap();
    const float listWidth = (std::min)(260.0f,
        ImGui::GetContentRegionAvail().x * 0.3f);
    if (ImGui::BeginChild("##ContextBrowser", ImVec2(listWidth, 0.0f), true)) {
        if (ImGui::Button("+ New Context", ImVec2(126.0f, 0.0f))) {
            addContextRequested_ = true;
        }
        ImGui::Separator();
        for (int index = 0; index < static_cast<int>(map.contexts.size()); ++index) {
            const INPUT::InputContextDefinition& context = map.contexts[index];
            ImGui::PushID(index);
            if (ImGui::Selectable(
                    context.displayName.c_str(), selectedContext_ == index)) {
                selectedContext_ = index;
            }
            ImGui::SameLine();
            if (inputService.Contexts().IsActive(context.contextId)) {
                ImGui::TextColored(
                    ImVec4(0.42f, 0.9f, 0.5f, 1.0f), "Active");
            }
            ImGui::TextDisabled(
                "%s  |  Priority %d", context.contextId.c_str(), context.priority);
            ImGui::TextDisabled("%zu bindings", context.bindings.size());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    if (ImGui::BeginChild("##ContextDetails", ImVec2(0.0f, 0.0f), true)) {
        if (map.contexts.empty()) {
            ImGui::TextDisabled("Create a context to begin.");
        } else {
            INPUT::InputContextDefinition& context = map.contexts[selectedContext_];
            ImGui::TextDisabled("CONTEXT DETAILS");
            ImGui::SameLine();
            ImGui::Text("%s", context.contextId.c_str());
            ImGui::Separator();

            ImGui::SetNextItemWidth(260.0f);
            if (EditString("Name", context.displayName)) dirty_ = true;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::DragInt(
                    "Priority", &context.priority, 1.0f, -10000, 10000)) {
                dirty_ = true;
            }
            if (ImGui::Checkbox("Consume active controls", &context.consumeInput)) {
                dirty_ = true;
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Enabled by default", &context.enabledByDefault)) {
                dirty_ = true;
            }
            ImGui::SameLine();
            bool runtimeActive = inputService.Contexts().IsActive(context.contextId);
            if (ImGui::Checkbox("Runtime active", &runtimeActive)) {
                inputService.Contexts().SetActive(context.contextId, runtimeActive);
            }

            if (ImGui::Button("+ Add Binding...")) {
                BeginBindingEdit(map, -1);
            }
            ImGui::SameLine();
            const bool hasAxis2D = std::any_of(
                map.actions.begin(), map.actions.end(),
                [](const INPUT::InputActionDefinition& action) {
                    return action.valueType ==
                        INPUT::InputActionValueType::Axis2D;
                });
            ImGui::BeginDisabled(!hasAxis2D);
            if (ImGui::Button("+ Add 2D Composite...")) {
                BeginCompositeEdit(map);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Delete Context...")) {
                deleteContextRequested_ = true;
            }

            if (ImGui::BeginTable(
                    "ContextBindings", 7,
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_BordersInnerV |
                    ImGuiTableFlags_ScrollY |
                    ImGuiTableFlags_Resizable |
                    ImGuiTableFlags_SizingStretchProp,
                    ImVec2(0.0f, -4.0f))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Action");
                ImGui::TableSetupColumn("Device / Source");
                ImGui::TableSetupColumn("Control");
                ImGui::TableSetupColumn("Modifier");
                ImGui::TableSetupColumn("Value");
                ImGui::TableSetupColumn("Dead Zone", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 92.0f);
                ImGui::TableHeadersRow();
                for (int index = 0;
                        index < static_cast<int>(context.bindings.size());
                        ++index) {
                    const INPUT::InputBinding& binding = context.bindings[index];
                    ImGui::PushID(index);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(binding.actionId.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(INPUT::ToString(binding.source));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(binding.control.c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextDisabled(
                        "%s", binding.modifierControl.empty()
                            ? "None" : binding.modifierControl.c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text(
                        "%.2f -> %s", binding.scale,
                        binding.component == 0 ? "X" : "Y");
                    ImGui::TableSetColumnIndex(5);
                    ImGui::Text("%.2f", binding.deadZone);
                    ImGui::TableSetColumnIndex(6);
                    if (ImGui::SmallButton("Edit")) {
                        BeginBindingEdit(map, index);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) {
                        bindingRemoveIndex_ = index;
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
    }
    ImGui::EndChild();
#else
    (void)inputService;
#endif
}

void InputActionMapEditor::DrawContextPopups(
    INPUT::InputService& inputService) {
#if defined(HIKARI_WITH_EDITOR)
    INPUT::InputActionMap& map = inputService.EditActionMap();
    if (addContextRequested_) {
        ImGui::OpenPopup("Add Input Context");
        addContextRequested_ = false;
    }
    if (ImGui::BeginPopupModal(
            "Add Input Context", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "Contexts group bindings that can be activated independently at runtime.");
        ImGui::SetNextItemWidth(340.0f);
        ImGui::InputText("Context ID", newContextId_.data(), newContextId_.size());
        ImGui::SetNextItemWidth(340.0f);
        ImGui::InputText(
            "Display Name", newContextName_.data(), newContextName_.size());
        const bool canAdd = newContextId_[0] != '\0' &&
            map.FindContext(newContextId_.data()) == nullptr;
        ImGui::BeginDisabled(!canAdd);
        if (ImGui::Button("Create Context", ImVec2(140.0f, 0.0f))) {
            INPUT::InputContextDefinition context{};
            context.contextId = newContextId_.data();
            context.displayName = newContextName_[0] != '\0'
                ? newContextName_.data() : newContextId_.data();
            map.contexts.push_back(std::move(context));
            selectedContext_ = static_cast<int>(map.contexts.size()) - 1;
            newContextId_.fill('\0');
            newContextName_.fill('\0');
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

    if (deleteContextRequested_) {
        ImGui::OpenPopup("Delete Input Context");
        deleteContextRequested_ = false;
    }
    if (ImGui::BeginPopupModal(
            "Delete Input Context", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!map.contexts.empty()) {
            const std::string id = map.contexts[selectedContext_].contextId;
            ImGui::TextWrapped(
                "Delete context %s and all of its bindings?", id.c_str());
            if (ImGui::Button("Delete", ImVec2(120.0f, 0.0f))) {
                inputService.Contexts().SetActive(id, false);
                (void)map.RemoveContext(id);
                selectedContext_ = (std::max)(0, selectedContext_ - 1);
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
