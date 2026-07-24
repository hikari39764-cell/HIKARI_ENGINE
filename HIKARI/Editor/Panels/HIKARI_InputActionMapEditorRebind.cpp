#include "Editor/Panels/HIKARI_InputActionMapEditor.h"

#include <algorithm>
#include <cmath>

#include "Input/Runtime/HIKARI_InputService.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

void InputActionMapEditor::BeginBindingCapture(
    INPUT::InputService& inputService) {
    const INPUT::InputRebindOperation& operation =
        inputService.GetRebindOperation();
    if (operation.GetStatus() != INPUT::InputRebindStatus::Idle) {
        bindingCaptureMessage_ =
            "Another input capture session is already active.";
        return;
    }

    INPUT::InputRebindOptions options{};
    options.allowMouseMotion = false;
    options.timeoutSeconds = 10.0f;
    if (!inputService.BeginRebind(options)) {
        bindingCaptureMessage_ = "Could not start input capture.";
        return;
    }
    rebindOwner_ = RebindOwner::Binding;
    bindingCaptureMessage_.clear();
    bindingApplyPending_ = false;
}

void InputActionMapEditor::DrawBindingCapture(
    INPUT::InputService& inputService) {
#if defined(HIKARI_WITH_EDITOR)
    if (rebindOwner_ == RebindOwner::Binding) {
        const INPUT::InputRebindOperation& operation =
            inputService.GetRebindOperation();
        const INPUT::InputRebindStatus status = operation.GetStatus();
        if (status == INPUT::InputRebindStatus::Captured) {
            INPUT::InputRebindResult result{};
            if (inputService.ConsumeRebindResult(result)) {
                bindingDraft_.source = result.source;
                bindingDraft_.control = result.control;
                if (result.source == INPUT::InputBindingSource::GamepadAxis) {
                    bindingDraft_.deadZone =
                        (std::max)(bindingDraft_.deadZone, 0.15f);
                }
                const INPUT::InputActionDefinition* action =
                    inputService.GetActionMap().FindAction(
                        bindingDraft_.actionId);
                if (action != nullptr &&
                    action->valueType != INPUT::InputActionValueType::Button &&
                    (result.source == INPUT::InputBindingSource::GamepadAxis ||
                     result.source == INPUT::InputBindingSource::MouseWheel) &&
                    std::abs(result.actuation) > 1e-5f) {
                    const float magnitude =
                        (std::max)(std::abs(bindingDraft_.scale), 1.0f);
                    bindingDraft_.scale =
                        std::copysign(magnitude, result.actuation);
                }
                bindingCaptureMessage_ = "Captured " +
                    std::string(INPUT::ToString(result.source)) +
                    " / " + result.control + ".";
                bindingApplyPending_ = false;
            }
            rebindOwner_ = RebindOwner::None;
        } else if (status == INPUT::InputRebindStatus::Cancelled) {
            bindingCaptureMessage_ = "Input capture cancelled.";
            inputService.ResetRebind();
            rebindOwner_ = RebindOwner::None;
        } else if (status == INPUT::InputRebindStatus::TimedOut) {
            bindingCaptureMessage_ = "Input capture timed out.";
            inputService.ResetRebind();
            rebindOwner_ = RebindOwner::None;
        }
    }

    if (rebindOwner_ == RebindOwner::Binding) {
        const INPUT::InputRebindOperation& operation =
            inputService.GetRebindOperation();
        ImGui::BeginChild(
            "##BindingCaptureStatus", ImVec2(0.0f, 62.0f), true);
        if (operation.GetStatus() ==
                INPUT::InputRebindStatus::WaitingForRelease) {
            ImGui::TextUnformatted(
                "Release the control used to click Listen...");
        } else {
            ImGui::TextUnformatted(
                "Press a key, mouse button, mouse wheel, or gamepad control.");
        }
        ImGui::TextDisabled(
            "Escape cancels  |  %.1f seconds remaining",
            operation.GetRemainingSeconds());
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel Listening")) {
            inputService.CancelRebind();
            inputService.ResetRebind();
            rebindOwner_ = RebindOwner::None;
            bindingCaptureMessage_ = "Input capture cancelled.";
        }
        ImGui::EndChild();
    } else if (!bindingCaptureMessage_.empty()) {
        ImGui::TextDisabled("%s", bindingCaptureMessage_.c_str());
    }
#else
    (void)inputService;
#endif
}

std::vector<int> InputActionMapEditor::FindBindingConflictIndices(
    const INPUT::InputActionMap& map) const {
    std::vector<int> conflicts;
    if (selectedContext_ < 0 ||
        selectedContext_ >= static_cast<int>(map.contexts.size()) ||
        bindingDraft_.control.empty()) {
        return conflicts;
    }
    const auto& bindings = map.contexts[selectedContext_].bindings;
    for (int index = 0; index < static_cast<int>(bindings.size()); ++index) {
        if (index == bindingEditIndex_) continue;
        const INPUT::InputBinding& binding = bindings[index];
        if (binding.source == bindingDraft_.source &&
            binding.control == bindingDraft_.control &&
            binding.modifierControl == bindingDraft_.modifierControl) {
            conflicts.push_back(index);
        }
    }
    return conflicts;
}

void InputActionMapEditor::ApplyBindingDraft(
    INPUT::InputActionMap& map,
    bool replaceConflicts) {
    if (selectedContext_ < 0 ||
        selectedContext_ >= static_cast<int>(map.contexts.size())) {
        return;
    }
    INPUT::InputContextDefinition& context = map.contexts[selectedContext_];
    int targetIndex = bindingEditIndex_;
    if (replaceConflicts) {
        std::vector<int> conflicts = FindBindingConflictIndices(map);
        std::sort(conflicts.rbegin(), conflicts.rend());
        for (int index : conflicts) {
            context.bindings.erase(context.bindings.begin() + index);
            if (targetIndex > index) --targetIndex;
        }
    }
    if (targetIndex >= 0 &&
        targetIndex < static_cast<int>(context.bindings.size())) {
        context.bindings[targetIndex] = bindingDraft_;
    } else {
        context.bindings.push_back(bindingDraft_);
        targetIndex = static_cast<int>(context.bindings.size()) - 1;
    }
    bindingEditIndex_ = targetIndex;
    bindingApplyPending_ = false;
    bindingConflictIndices_.clear();
    dirty_ = true;
}

bool InputActionMapEditor::DrawBindingConflictResolution(
    INPUT::InputActionMap& map) {
#if defined(HIKARI_WITH_EDITOR)
    bindingConflictIndices_ = FindBindingConflictIndices(map);
    if (bindingConflictIndices_.empty()) {
        ApplyBindingDraft(map, false);
        return true;
    }

    const auto& bindings = map.contexts[selectedContext_].bindings;
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(
        ImVec4(1.0f, 0.68f, 0.25f, 1.0f),
        "This control is already used by %zu binding(s):",
        bindingConflictIndices_.size());
    for (int index : bindingConflictIndices_) {
        if (index >= 0 && index < static_cast<int>(bindings.size())) {
            ImGui::BulletText("%s", bindings[index].actionId.c_str());
        }
    }
    ImGui::TextWrapped(
        "Replace removes those bindings from this context. Keep Both leaves "
        "the shared control intentionally mapped to multiple actions.");
    if (ImGui::Button("Replace Existing & Apply", ImVec2(190.0f, 0.0f))) {
        ApplyBindingDraft(map, true);
        return true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Keep Both & Apply", ImVec2(155.0f, 0.0f))) {
        ApplyBindingDraft(map, false);
        return true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Go Back", ImVec2(90.0f, 0.0f))) {
        bindingApplyPending_ = false;
        bindingConflictIndices_.clear();
    }
#else
    (void)map;
#endif
    return false;
}

} // namespace HIKARI
