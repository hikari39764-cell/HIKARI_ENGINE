#include "Editor/Panels/HIKARI_InputActionMapEditor.h"

#include "Core/Text/HIKARI_Localization.h"
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
        if (ImGui::Button(
                TEXT::Localize("ui.input.newContext").c_str(),
                ImVec2(126.0f, 0.0f))) {
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
                    ImVec4(0.42f, 0.9f, 0.5f, 1.0f),
                    "%s",
                    TEXT::Localize("ui.input.active").c_str());
            }
            ImGui::TextDisabled(
                TEXT::Localize(
                    "ui.input.contextPriorityFormat").c_str(),
                context.contextId.c_str(),
                context.priority);
            ImGui::TextDisabled(
                TEXT::Localize("ui.input.bindingsCountFormat").c_str(),
                context.bindings.size());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    if (ImGui::BeginChild("##ContextDetails", ImVec2(0.0f, 0.0f), true)) {
        if (map.contexts.empty()) {
            ImGui::TextDisabled(
                "%s",
                TEXT::Localize(
                    "ui.input.createContextToBegin").c_str());
        } else {
            INPUT::InputContextDefinition& context = map.contexts[selectedContext_];
            ImGui::TextDisabled(
                "%s",
                TEXT::Localize("ui.input.contextDetails").c_str());
            ImGui::SameLine();
            ImGui::Text("%s", context.contextId.c_str());
            ImGui::Separator();

            ImGui::SetNextItemWidth(260.0f);
            if (EditString(
                    TEXT::Localize("ui.input.name").c_str(),
                    context.displayName)) dirty_ = true;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::DragInt(
                    TEXT::Localize("ui.input.priority").c_str(),
                    &context.priority, 1.0f, -10000, 10000)) {
                dirty_ = true;
            }
            if (ImGui::Checkbox(
                    TEXT::Localize(
                        "ui.input.consumeControls").c_str(),
                    &context.consumeInput)) {
                dirty_ = true;
            }
            ImGui::SameLine();
            if (ImGui::Checkbox(
                    TEXT::Localize(
                        "ui.input.enabledByDefault").c_str(),
                    &context.enabledByDefault)) {
                dirty_ = true;
            }
            ImGui::SameLine();
            bool runtimeActive = inputService.Contexts().IsActive(context.contextId);
            if (ImGui::Checkbox(
                    TEXT::Localize(
                        "ui.input.runtimeActive").c_str(),
                    &runtimeActive)) {
                inputService.Contexts().SetActive(context.contextId, runtimeActive);
            }

            if (ImGui::Button(
                    TEXT::Localize("ui.input.addBinding").c_str())) {
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
            if (ImGui::Button(
                    TEXT::Localize(
                        "ui.input.add2DComposite").c_str())) {
                BeginCompositeEdit(map);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button(
                    TEXT::Localize(
                        "ui.input.deleteContext").c_str())) {
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
                ImGui::TableSetupColumn(
                    TEXT::Localize("ui.input.tableAction").c_str());
                ImGui::TableSetupColumn(
                    TEXT::Localize(
                        "ui.input.tableDeviceSource").c_str());
                ImGui::TableSetupColumn(
                    TEXT::Localize("ui.input.tableControl").c_str());
                ImGui::TableSetupColumn(
                    TEXT::Localize("ui.input.tableModifier").c_str());
                ImGui::TableSetupColumn(
                    TEXT::Localize("ui.input.tableValue").c_str());
                ImGui::TableSetupColumn(
                    TEXT::Localize("ui.input.tableDeadZone").c_str(),
                    ImGuiTableColumnFlags_WidthFixed, 80.0f);
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
                        "%s",
                        binding.modifierControl.empty()
                            ? TEXT::Localize("ui.input.none").c_str()
                            : binding.modifierControl.c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text(
                        "%.2f -> %s", binding.scale,
                        binding.component == 0 ? "X" : "Y");
                    ImGui::TableSetColumnIndex(5);
                    ImGui::Text("%.2f", binding.deadZone);
                    ImGui::TableSetColumnIndex(6);
                    if (ImGui::SmallButton(
                            TEXT::Localize("ui.input.edit").c_str())) {
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
        ImGui::OpenPopup(
            (TEXT::Localize("ui.input.addContextTitle") +
                "###AddInputContext").c_str());
        addContextRequested_ = false;
    }
    if (ImGui::BeginPopupModal(
            (TEXT::Localize("ui.input.addContextTitle") +
                "###AddInputContext").c_str(),
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "%s",
            TEXT::Localize("ui.input.addContextHint").c_str());
        ImGui::SetNextItemWidth(340.0f);
        ImGui::InputText(
            TEXT::Localize("ui.input.contextId").c_str(),
            newContextId_.data(),
            newContextId_.size());
        ImGui::SetNextItemWidth(340.0f);
        ImGui::InputText(
            TEXT::Localize("ui.input.displayName").c_str(),
            newContextName_.data(),
            newContextName_.size());
        const bool canAdd = newContextId_[0] != '\0' &&
            map.FindContext(newContextId_.data()) == nullptr;
        ImGui::BeginDisabled(!canAdd);
        if (ImGui::Button(
                TEXT::Localize("ui.input.createContext").c_str(),
                ImVec2(140.0f, 0.0f))) {
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
        if (ImGui::Button(
                TEXT::Localize("ui.common.cancel").c_str(),
                ImVec2(110.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (deleteContextRequested_) {
        ImGui::OpenPopup(
            (TEXT::Localize("ui.input.deleteContextTitle") +
                "###DeleteInputContext").c_str());
        deleteContextRequested_ = false;
    }
    if (ImGui::BeginPopupModal(
            (TEXT::Localize("ui.input.deleteContextTitle") +
                "###DeleteInputContext").c_str(),
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!map.contexts.empty()) {
            const std::string id = map.contexts[selectedContext_].contextId;
            ImGui::TextWrapped(
                TEXT::Localize(
                    "ui.input.deleteContextBodyFormat").c_str(),
                id.c_str());
            if (ImGui::Button(
                    TEXT::Localize("ui.common.delete").c_str(),
                    ImVec2(120.0f, 0.0f))) {
                inputService.Contexts().SetActive(id, false);
                (void)map.RemoveContext(id);
                selectedContext_ = (std::max)(0, selectedContext_ - 1);
                dirty_ = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(
                TEXT::Localize("ui.common.cancel").c_str(),
                ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
#else
    (void)inputService;
#endif
}

} // namespace HIKARI
