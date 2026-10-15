#include "Editor/Panels/HIKARI_InputActionMapEditor.h"

#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Panels/HIKARI_InputActionMapEditorWidgets.h"
#include "Input/Runtime/HIKARI_InputService.h"

#include "imgui.h"

namespace HIKARI {

void InputActionMapEditor::BeginBindingEdit(const INPUT::InputActionMap& map, int bindingIndex) {
    bindingEditIndex_ = bindingIndex;
    if (bindingIndex >= 0 && selectedContext_ < static_cast<int>(map.contexts.size()) &&
        bindingIndex < static_cast<int>(map.contexts[selectedContext_].bindings.size())) {
        bindingDraft_ = map.contexts[selectedContext_].bindings[bindingIndex];
    } else {
        bindingDraft_ = {};
        bindingDraft_.actionId = map.actions.empty() ? std::string{} : map.actions.front().actionId;
        bindingDraft_.source = INPUT::InputBindingSource::Keyboard;
        bindingDraft_.control = "Space";
    }
    bindingApplyPending_ = false;
    bindingConflictIndices_.clear();
    bindingCaptureMessage_.clear();
    bindingPopupRequested_ = true;
}

void InputActionMapEditor::DrawBindingPopup(INPUT::InputService& inputService) {
    using namespace EDITOR::INPUT_WIDGETS;
    INPUT::InputActionMap& map = inputService.EditActionMap();
    if (bindingPopupRequested_) {
        ImGui::OpenPopup((TEXT::Localize("ui.input.bindingTitle") + "###InputBinding").c_str());
        bindingPopupRequested_ = false;
    }
    if (ImGui::BeginPopupModal((TEXT::Localize("ui.input.bindingTitle") + "###InputBinding").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("%s", bindingEditIndex_ >= 0 ? TEXT::Localize("ui.input.editBinding").c_str()
                                                         : TEXT::Localize("ui.input.newBinding").c_str());
        ImGui::Separator();
        ImGui::SetNextItemWidth(390.0f);
        if (ImGui::BeginCombo(TEXT::Localize("ui.input.action").c_str(),
                              bindingDraft_.actionId.empty() ? TEXT::Localize("ui.input.selectAction").c_str()
                                                             : bindingDraft_.actionId.c_str())) {
            for (const INPUT::InputActionDefinition& action : map.actions) {
                const bool selected = action.actionId == bindingDraft_.actionId;
                if (ImGui::Selectable(action.actionId.c_str(), selected)) {
                    bindingDraft_.actionId = action.actionId;
                    bindingApplyPending_ = false;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::SetNextItemWidth(250.0f);
        if (DrawBindingSourceCombo(TEXT::Localize("ui.input.deviceSource").c_str(), bindingDraft_.source)) {
            bindingDraft_.control = DefaultControl(bindingDraft_.source);
            bindingDraft_.modifierControl.clear();
            bindingDraft_.deadZone = bindingDraft_.source == INPUT::InputBindingSource::GamepadAxis ? 0.15f : 0.0f;
            bindingApplyPending_ = false;
        }
        ImGui::SetNextItemWidth(250.0f);
        if (DrawControlPicker(TEXT::Localize("ui.input.control").c_str(), bindingDraft_.source,
                              bindingDraft_.control)) {
            bindingApplyPending_ = false;
        }
        ImGui::SameLine();
        const bool bindingListening =
            rebindOwner_ == RebindOwner::Binding && inputService.GetRebindOperation().IsActive();
        ImGui::BeginDisabled(bindingListening);
        if (ImGui::Button(TEXT::Localize("ui.input.listen").c_str(), ImVec2(92.0f, 0.0f))) {
            BeginBindingCapture(inputService);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(TEXT::Localize("ui.input.clear").c_str(), ImVec2(70.0f, 0.0f))) {
            if (rebindOwner_ == RebindOwner::Binding) {
                inputService.CancelRebind();
                inputService.ResetRebind();
                rebindOwner_ = RebindOwner::None;
            }
            bindingDraft_.control.clear();
            bindingApplyPending_ = false;
        }
        DrawBindingCapture(inputService);
        ImGui::SetNextItemWidth(390.0f);
        if (EditString(TEXT::Localize("ui.input.customControl").c_str(), bindingDraft_.control, 128)) {
            bindingApplyPending_ = false;
        }
        ImGui::TextDisabled("%s", TEXT::Localize("ui.input.customControlHint").c_str());

        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::DragFloat(TEXT::Localize("ui.input.scale").c_str(), &bindingDraft_.scale, 0.05f, -20.0f, 20.0f,
                             "%.2f")) {
            bindingApplyPending_ = false;
        }
        const INPUT::InputActionDefinition* action = map.FindAction(bindingDraft_.actionId);
        if (action != nullptr && action->valueType == INPUT::InputActionValueType::Axis2D) {
            int component = bindingDraft_.component;
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::Combo(TEXT::Localize("ui.input.outputComponent").c_str(), &component, "X\0Y\0")) {
                bindingDraft_.component = static_cast<uint8_t>(component);
                bindingApplyPending_ = false;
            }
        } else {
            bindingDraft_.component = 0;
        }
        ImGui::SetNextItemWidth(250.0f);
        if (DrawModifierPicker(bindingDraft_.modifierControl)) {
            bindingApplyPending_ = false;
        }
        ImGui::SetNextItemWidth(390.0f);
        if (EditString(TEXT::Localize("ui.input.customModifier").c_str(), bindingDraft_.modifierControl, 128)) {
            bindingApplyPending_ = false;
        }
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::DragFloat(TEXT::Localize("ui.input.deadZone").c_str(), &bindingDraft_.deadZone, 0.01f, 0.0f, 0.99f,
                             "%.2f")) {
            bindingApplyPending_ = false;
        }

        const bool canApply = !bindingDraft_.actionId.empty() && !bindingDraft_.control.empty() &&
                              !map.contexts.empty() && !bindingListening;
        bool closeBindingPopup = false;
        ImGui::BeginDisabled(!canApply);
        if (ImGui::Button(bindingEditIndex_ >= 0 ? TEXT::Localize("ui.input.applyChanges").c_str()
                                                 : TEXT::Localize("ui.input.addBinding").c_str(),
                          ImVec2(140.0f, 0.0f))) {
            bindingConflictIndices_ = FindBindingConflictIndices(map);
            if (bindingConflictIndices_.empty()) {
                ApplyBindingDraft(map, false);
                closeBindingPopup = true;
            } else {
                bindingApplyPending_ = true;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(TEXT::Localize("ui.common.cancel").c_str(), ImVec2(110.0f, 0.0f))) {
            if (rebindOwner_ == RebindOwner::Binding) {
                inputService.CancelRebind();
                inputService.ResetRebind();
                rebindOwner_ = RebindOwner::None;
            }
            bindingApplyPending_ = false;
            closeBindingPopup = true;
        }
        if (bindingApplyPending_) {
            closeBindingPopup = DrawBindingConflictResolution(map) || closeBindingPopup;
        }
        if (closeBindingPopup)
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (bindingRemoveIndex_ >= 0) {
        ImGui::OpenPopup((TEXT::Localize("ui.input.removeBindingTitle") + "###RemoveInputBinding").c_str());
    }
    if (ImGui::BeginPopupModal((TEXT::Localize("ui.input.removeBindingTitle") + "###RemoveInputBinding").c_str(),
                               nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(TEXT::Localize("ui.input.removeBindingBody").c_str());
        if (ImGui::Button(TEXT::Localize("ui.input.remove").c_str(), ImVec2(120.0f, 0.0f))) {
            if (!map.contexts.empty()) {
                auto& bindings = map.contexts[selectedContext_].bindings;
                if (bindingRemoveIndex_ >= 0 && bindingRemoveIndex_ < static_cast<int>(bindings.size())) {
                    bindings.erase(bindings.begin() + bindingRemoveIndex_);
                    dirty_ = true;
                }
            }
            bindingRemoveIndex_ = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(TEXT::Localize("ui.common.cancel").c_str(), ImVec2(120.0f, 0.0f))) {
            bindingRemoveIndex_ = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace HIKARI
