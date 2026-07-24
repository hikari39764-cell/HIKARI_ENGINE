#include "Editor/Panels/HIKARI_InputActionMapEditor.h"

#include "Editor/Panels/HIKARI_InputActionMapEditorWidgets.h"
#include "Input/Runtime/HIKARI_InputService.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

void InputActionMapEditor::BeginBindingEdit(
    const INPUT::InputActionMap& map,
    int bindingIndex) {
    bindingEditIndex_ = bindingIndex;
    if (bindingIndex >= 0 && selectedContext_ <
            static_cast<int>(map.contexts.size()) && bindingIndex <
            static_cast<int>(map.contexts[selectedContext_].bindings.size())) {
        bindingDraft_ = map.contexts[selectedContext_].bindings[bindingIndex];
    } else {
        bindingDraft_ = {};
        bindingDraft_.actionId = map.actions.empty()
            ? std::string{} : map.actions.front().actionId;
        bindingDraft_.source = INPUT::InputBindingSource::Keyboard;
        bindingDraft_.control = "Space";
    }
    bindingApplyPending_ = false;
    bindingConflictIndices_.clear();
    bindingCaptureMessage_.clear();
    bindingPopupRequested_ = true;
}

void InputActionMapEditor::DrawBindingPopup(
    INPUT::InputService& inputService) {
#if defined(HIKARI_WITH_EDITOR)
    using namespace EDITOR::INPUT_WIDGETS;
    INPUT::InputActionMap& map = inputService.EditActionMap();
    if (bindingPopupRequested_) {
        ImGui::OpenPopup("Input Binding");
        bindingPopupRequested_ = false;
    }
    if (ImGui::BeginPopupModal(
            "Input Binding", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled(
            bindingEditIndex_ >= 0 ? "EDIT BINDING" : "NEW BINDING");
        ImGui::Separator();
        ImGui::SetNextItemWidth(390.0f);
        if (ImGui::BeginCombo(
                "Action", bindingDraft_.actionId.empty()
                    ? "Select action" : bindingDraft_.actionId.c_str())) {
            for (const INPUT::InputActionDefinition& action : map.actions) {
                const bool selected = action.actionId == bindingDraft_.actionId;
                if (ImGui::Selectable(action.actionId.c_str(), selected)) {
                    bindingDraft_.actionId = action.actionId;
                    bindingApplyPending_ = false;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::SetNextItemWidth(250.0f);
        if (DrawBindingSourceCombo("Device Source", bindingDraft_.source)) {
            bindingDraft_.control = DefaultControl(bindingDraft_.source);
            bindingDraft_.modifierControl.clear();
            bindingDraft_.deadZone =
                bindingDraft_.source == INPUT::InputBindingSource::GamepadAxis
                    ? 0.15f : 0.0f;
            bindingApplyPending_ = false;
        }
        ImGui::SetNextItemWidth(250.0f);
        if (DrawControlPicker(
                "Control", bindingDraft_.source, bindingDraft_.control)) {
            bindingApplyPending_ = false;
        }
        ImGui::SameLine();
        const bool bindingListening = rebindOwner_ == RebindOwner::Binding &&
            inputService.GetRebindOperation().IsActive();
        ImGui::BeginDisabled(bindingListening);
        if (ImGui::Button("Listen...", ImVec2(92.0f, 0.0f))) {
            BeginBindingCapture(inputService);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Clear", ImVec2(70.0f, 0.0f))) {
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
        if (EditString("Custom Control", bindingDraft_.control, 128)) {
            bindingApplyPending_ = false;
        }
        ImGui::TextDisabled(
            "Use the picker for standard controls; custom names remain available for extensions.");

        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::DragFloat(
            "Scale", &bindingDraft_.scale, 0.05f,
            -20.0f, 20.0f, "%.2f")) {
            bindingApplyPending_ = false;
        }
        const INPUT::InputActionDefinition* action =
            map.FindAction(bindingDraft_.actionId);
        if (action != nullptr &&
            action->valueType == INPUT::InputActionValueType::Axis2D) {
            int component = bindingDraft_.component;
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::Combo("Output Component", &component, "X\0Y\0")) {
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
        if (EditString(
                "Custom Modifier", bindingDraft_.modifierControl, 128)) {
            bindingApplyPending_ = false;
        }
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::DragFloat(
            "Dead Zone", &bindingDraft_.deadZone, 0.01f,
            0.0f, 0.99f, "%.2f")) {
            bindingApplyPending_ = false;
        }

        const bool canApply = !bindingDraft_.actionId.empty() &&
            !bindingDraft_.control.empty() && !map.contexts.empty() &&
            !bindingListening;
        bool closeBindingPopup = false;
        ImGui::BeginDisabled(!canApply);
        if (ImGui::Button(
                bindingEditIndex_ >= 0 ? "Apply Changes" : "Add Binding",
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
        if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f))) {
            if (rebindOwner_ == RebindOwner::Binding) {
                inputService.CancelRebind();
                inputService.ResetRebind();
                rebindOwner_ = RebindOwner::None;
            }
            bindingApplyPending_ = false;
            closeBindingPopup = true;
        }
        if (bindingApplyPending_) {
            closeBindingPopup =
                DrawBindingConflictResolution(map) || closeBindingPopup;
        }
        if (closeBindingPopup) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (bindingRemoveIndex_ >= 0) {
        ImGui::OpenPopup("Remove Input Binding");
    }
    if (ImGui::BeginPopupModal(
            "Remove Input Binding", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Remove this binding from the selected context?");
        if (ImGui::Button("Remove", ImVec2(120.0f, 0.0f))) {
            if (!map.contexts.empty()) {
                auto& bindings = map.contexts[selectedContext_].bindings;
                if (bindingRemoveIndex_ >= 0 && bindingRemoveIndex_ <
                        static_cast<int>(bindings.size())) {
                    bindings.erase(bindings.begin() + bindingRemoveIndex_);
                    dirty_ = true;
                }
            }
            bindingRemoveIndex_ = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            bindingRemoveIndex_ = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
#else
    (void)inputService;
#endif
}

} // namespace HIKARI
