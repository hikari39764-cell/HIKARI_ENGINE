#include "Editor/Panels/HIKARI_InputActionMapEditor.h"

#include <algorithm>
#include <array>
#include <unordered_set>
#include <utility>

#include "Input/Runtime/HIKARI_InputService.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {
namespace {

constexpr std::array<const char*, 4> kDirectionNames{
    "Up", "Down", "Left", "Right" };
constexpr std::array<float, 4> kDirectionScales{
    1.0f, -1.0f, -1.0f, 1.0f };
constexpr std::array<uint8_t, 4> kDirectionComponents{
    1, 1, 0, 0 };

bool SamePhysicalControl(
    const INPUT::InputBinding& lhs,
    const INPUT::InputBinding& rhs) {
    return lhs.source == rhs.source && lhs.control == rhs.control &&
        lhs.modifierControl == rhs.modifierControl;
}

INPUT::InputRebindOptions CompositeCaptureOptions() {
    INPUT::InputRebindOptions options{};
    options.allowMouseWheel = false;
    options.allowMouseMotion = false;
    options.allowGamepadAxes = false;
    options.timeoutSeconds = 10.0f;
    return options;
}

} // namespace

void InputActionMapEditor::BeginCompositeEdit(
    const INPUT::InputActionMap& map) {
    compositeActionId_.clear();
    if (selectedAction_ >= 0 &&
        selectedAction_ < static_cast<int>(map.actions.size()) &&
        map.actions[selectedAction_].valueType ==
            INPUT::InputActionValueType::Axis2D) {
        compositeActionId_ = map.actions[selectedAction_].actionId;
    } else {
        const auto it = std::find_if(
            map.actions.begin(), map.actions.end(),
            [](const INPUT::InputActionDefinition& action) {
                return action.valueType == INPUT::InputActionValueType::Axis2D;
            });
        if (it != map.actions.end()) compositeActionId_ = it->actionId;
    }
    compositeBindings_ = {};
    compositeCaptured_.fill(false);
    compositeCaptureStep_ = -1;
    compositeCaptureMessage_.clear();
    compositeReplaceConflicts_ = true;
    compositePopupRequested_ = true;
}

void InputActionMapEditor::BeginCompositeCapture(
    INPUT::InputService& inputService) {
    if (compositeActionId_.empty()) {
        compositeCaptureMessage_ = "Select an Axis2D action first.";
        return;
    }
    if (inputService.GetRebindOperation().GetStatus() !=
            INPUT::InputRebindStatus::Idle) {
        compositeCaptureMessage_ =
            "Another input capture session is already active.";
        return;
    }
    compositeBindings_ = {};
    compositeCaptured_.fill(false);
    compositeCaptureStep_ = 0;
    compositeCaptureMessage_.clear();
    if (!inputService.BeginRebind(CompositeCaptureOptions())) {
        compositeCaptureStep_ = -1;
        compositeCaptureMessage_ = "Could not start input capture.";
        return;
    }
    rebindOwner_ = RebindOwner::Composite;
}

void InputActionMapEditor::DrawCompositeCapture(
    INPUT::InputService& inputService) {
#if defined(HIKARI_WITH_EDITOR)
    if (rebindOwner_ == RebindOwner::Composite) {
        const INPUT::InputRebindStatus status =
            inputService.GetRebindOperation().GetStatus();
        if (status == INPUT::InputRebindStatus::Captured) {
            INPUT::InputRebindResult result{};
            if (inputService.ConsumeRebindResult(result) &&
                compositeCaptureStep_ >= 0 && compositeCaptureStep_ < 4) {
                INPUT::InputBinding& binding =
                    compositeBindings_[compositeCaptureStep_];
                binding.actionId = compositeActionId_;
                binding.source = result.source;
                binding.control = result.control;
                binding.scale = kDirectionScales[compositeCaptureStep_];
                binding.component = kDirectionComponents[compositeCaptureStep_];
                compositeCaptured_[compositeCaptureStep_] = true;
                ++compositeCaptureStep_;
                if (compositeCaptureStep_ < 4) {
                    if (!inputService.BeginRebind(
                            CompositeCaptureOptions())) {
                        compositeCaptureMessage_ =
                            "Could not continue input capture.";
                        compositeCaptureStep_ = -1;
                        rebindOwner_ = RebindOwner::None;
                    }
                } else {
                    compositeCaptureMessage_ =
                        "All four directions captured. Review and add the composite.";
                    compositeCaptureStep_ = -1;
                    rebindOwner_ = RebindOwner::None;
                }
            }
        } else if (status == INPUT::InputRebindStatus::Cancelled ||
                   status == INPUT::InputRebindStatus::TimedOut) {
            compositeCaptureMessage_ = status ==
                    INPUT::InputRebindStatus::Cancelled
                ? "Direction capture cancelled. Captured directions were kept."
                : "Direction capture timed out. Captured directions were kept.";
            inputService.ResetRebind();
            compositeCaptureStep_ = -1;
            rebindOwner_ = RebindOwner::None;
        }
    }

    if (rebindOwner_ == RebindOwner::Composite &&
        compositeCaptureStep_ >= 0 && compositeCaptureStep_ < 4) {
        const INPUT::InputRebindOperation& operation =
            inputService.GetRebindOperation();
        ImGui::BeginChild(
            "##CompositeCaptureStatus", ImVec2(0.0f, 66.0f), true);
        if (operation.GetStatus() ==
                INPUT::InputRebindStatus::WaitingForRelease) {
            ImGui::Text("Release the previous control, then press %s...",
                kDirectionNames[compositeCaptureStep_]);
        } else {
            ImGui::Text("Press the control for %s...",
                kDirectionNames[compositeCaptureStep_]);
        }
        ImGui::TextDisabled(
            "Escape cancels  |  %.1f seconds remaining",
            operation.GetRemainingSeconds());
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel Capture")) {
            inputService.CancelRebind();
            inputService.ResetRebind();
            compositeCaptureStep_ = -1;
            rebindOwner_ = RebindOwner::None;
            compositeCaptureMessage_ =
                "Direction capture cancelled. Captured directions were kept.";
        }
        ImGui::EndChild();
    } else if (!compositeCaptureMessage_.empty()) {
        ImGui::TextWrapped("%s", compositeCaptureMessage_.c_str());
    }
#else
    (void)inputService;
#endif
}

void InputActionMapEditor::DrawCompositePopup(
    INPUT::InputService& inputService) {
#if defined(HIKARI_WITH_EDITOR)
    INPUT::InputActionMap& map = inputService.EditActionMap();
    if (compositePopupRequested_) {
        ImGui::OpenPopup("Axis2D Composite Binding");
        compositePopupRequested_ = false;
    }
    if (!ImGui::BeginPopupModal(
            "Axis2D Composite Binding", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextDisabled("FOUR-DIRECTION COMPOSITE");
    ImGui::TextWrapped(
        "Capture four button-like controls. They are stored as independent "
        "bindings that together produce one Axis2D action.");
    ImGui::SetNextItemWidth(380.0f);
    if (ImGui::BeginCombo(
            "Action", compositeActionId_.empty()
                ? "Select Axis2D action" : compositeActionId_.c_str())) {
        for (const INPUT::InputActionDefinition& action : map.actions) {
            if (action.valueType != INPUT::InputActionValueType::Axis2D) continue;
            const bool selected = action.actionId == compositeActionId_;
            if (ImGui::Selectable(action.actionId.c_str(), selected)) {
                if (compositeActionId_ != action.actionId) {
                    if (rebindOwner_ == RebindOwner::Composite) {
                        inputService.CancelRebind();
                        inputService.ResetRebind();
                        rebindOwner_ = RebindOwner::None;
                    }
                    compositeActionId_ = action.actionId;
                    compositeBindings_ = {};
                    compositeCaptured_.fill(false);
                    compositeCaptureStep_ = -1;
                    compositeCaptureMessage_.clear();
                }
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (ImGui::BeginTable(
            "CompositeDirections", 3,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Direction", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Captured Control");
        ImGui::TableSetupColumn("Output", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();
        for (int index = 0; index < 4; ++index) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(kDirectionNames[index]);
            ImGui::TableSetColumnIndex(1);
            if (compositeCaptured_[index]) {
                ImGui::Text("%s / %s",
                    INPUT::ToString(compositeBindings_[index].source),
                    compositeBindings_[index].control.c_str());
            } else if (compositeCaptureStep_ == index &&
                       rebindOwner_ == RebindOwner::Composite) {
                ImGui::TextColored(
                    ImVec4(0.38f, 0.85f, 0.9f, 1.0f), "Listening...");
            } else {
                ImGui::TextDisabled("Not captured");
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s %.0f",
                kDirectionComponents[index] == 0 ? "X" : "Y",
                kDirectionScales[index]);
        }
        ImGui::EndTable();
    }

    DrawCompositeCapture(inputService);
    const bool capturing = rebindOwner_ == RebindOwner::Composite;
    ImGui::BeginDisabled(capturing || compositeActionId_.empty());
    if (ImGui::Button(
            compositeCaptured_[0] ? "Restart Capture" : "Start Capture",
            ImVec2(140.0f, 0.0f))) {
        BeginCompositeCapture(inputService);
    }
    ImGui::EndDisabled();

    const bool allCaptured = std::all_of(
        compositeCaptured_.begin(), compositeCaptured_.end(),
        [](bool captured) { return captured; });
    bool duplicateDirections = false;
    if (allCaptured) {
        for (int lhs = 0; lhs < 4; ++lhs) {
            for (int rhs = lhs + 1; rhs < 4; ++rhs) {
                duplicateDirections = duplicateDirections ||
                    SamePhysicalControl(
                        compositeBindings_[lhs], compositeBindings_[rhs]);
            }
        }
    }

    std::unordered_set<int> conflictIndices;
    if (allCaptured && selectedContext_ >= 0 &&
        selectedContext_ < static_cast<int>(map.contexts.size())) {
        const auto& existing = map.contexts[selectedContext_].bindings;
        for (int index = 0; index < static_cast<int>(existing.size()); ++index) {
            for (const INPUT::InputBinding& captured : compositeBindings_) {
                if (SamePhysicalControl(existing[index], captured)) {
                    conflictIndices.insert(index);
                }
            }
        }
    }
    if (duplicateDirections) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.45f, 0.32f, 1.0f),
            "Each direction needs a different physical control.");
    }
    if (!conflictIndices.empty()) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.68f, 0.25f, 1.0f),
            "%zu existing binding(s) use these controls.",
            conflictIndices.size());
        ImGui::Checkbox(
            "Replace existing conflicting bindings",
            &compositeReplaceConflicts_);
    }

    ImGui::Spacing();
    const bool canAdd = allCaptured && !duplicateDirections &&
        selectedContext_ >= 0 &&
        selectedContext_ < static_cast<int>(map.contexts.size());
    ImGui::BeginDisabled(!canAdd);
    if (ImGui::Button("Add Composite", ImVec2(140.0f, 0.0f))) {
        INPUT::InputContextDefinition& context = map.contexts[selectedContext_];
        if (compositeReplaceConflicts_ && !conflictIndices.empty()) {
            std::vector<int> ordered(
                conflictIndices.begin(), conflictIndices.end());
            std::sort(ordered.rbegin(), ordered.rend());
            for (int index : ordered) {
                context.bindings.erase(context.bindings.begin() + index);
            }
        }
        for (INPUT::InputBinding binding : compositeBindings_) {
            binding.actionId = compositeActionId_;
            context.bindings.push_back(std::move(binding));
        }
        dirty_ = true;
        SetStatus("Axis2D composite added.", false);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f))) {
        if (rebindOwner_ == RebindOwner::Composite) {
            inputService.CancelRebind();
            inputService.ResetRebind();
            rebindOwner_ = RebindOwner::None;
        }
        compositeCaptureStep_ = -1;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
#else
    (void)inputService;
#endif
}

} // namespace HIKARI
