#include "Editor/Panels/HIKARI_InputActionMapEditor.h"

#include "Core/Text/HIKARI_Localization.h"
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

constexpr std::array<const char*, 4> kDirectionNameKeys{
    "ui.input.directionUp",
    "ui.input.directionDown",
    "ui.input.directionLeft",
    "ui.input.directionRight",
};
constexpr std::array<float, 4> kDirectionScales{1.0f, -1.0f, -1.0f, 1.0f};
constexpr std::array<uint8_t, 4> kDirectionComponents{1, 1, 0, 0};

bool SamePhysicalControl(const INPUT::InputBinding& lhs, const INPUT::InputBinding& rhs) {
    return lhs.source == rhs.source && lhs.control == rhs.control && lhs.modifierControl == rhs.modifierControl;
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

void InputActionMapEditor::BeginCompositeEdit(const INPUT::InputActionMap& map) {
    compositeActionId_.clear();
    if (selectedAction_ >= 0 && selectedAction_ < static_cast<int>(map.actions.size()) &&
        map.actions[selectedAction_].valueType == INPUT::InputActionValueType::Axis2D) {
        compositeActionId_ = map.actions[selectedAction_].actionId;
    } else {
        const auto it =
            std::find_if(map.actions.begin(), map.actions.end(), [](const INPUT::InputActionDefinition& action) {
                return action.valueType == INPUT::InputActionValueType::Axis2D;
            });
        if (it != map.actions.end())
            compositeActionId_ = it->actionId;
    }
    compositeBindings_ = {};
    compositeCaptured_.fill(false);
    compositeCaptureStep_ = -1;
    compositeCaptureMessage_.clear();
    compositeReplaceConflicts_ = true;
    compositePopupRequested_ = true;
}

void InputActionMapEditor::BeginCompositeCapture(INPUT::InputService& inputService) {
    if (compositeActionId_.empty()) {
        compositeCaptureMessage_ = TEXT::Localize("ui.input.selectAxis2DFirst");
        return;
    }
    if (inputService.GetRebindOperation().GetStatus() != INPUT::InputRebindStatus::Idle) {
        compositeCaptureMessage_ = TEXT::Localize("ui.input.captureSessionActive");
        return;
    }
    compositeBindings_ = {};
    compositeCaptured_.fill(false);
    compositeCaptureStep_ = 0;
    compositeCaptureMessage_.clear();
    if (!inputService.BeginRebind(CompositeCaptureOptions())) {
        compositeCaptureStep_ = -1;
        compositeCaptureMessage_ = TEXT::Localize("ui.input.captureStartFailed");
        return;
    }
    rebindOwner_ = RebindOwner::Composite;
}

void InputActionMapEditor::DrawCompositeCapture(INPUT::InputService& inputService) {
#if defined(HIKARI_WITH_EDITOR)
    if (rebindOwner_ == RebindOwner::Composite) {
        const INPUT::InputRebindStatus status = inputService.GetRebindOperation().GetStatus();
        if (status == INPUT::InputRebindStatus::Captured) {
            INPUT::InputRebindResult result{};
            if (inputService.ConsumeRebindResult(result) && compositeCaptureStep_ >= 0 && compositeCaptureStep_ < 4) {
                INPUT::InputBinding& binding = compositeBindings_[compositeCaptureStep_];
                binding.actionId = compositeActionId_;
                binding.source = result.source;
                binding.control = result.control;
                binding.scale = kDirectionScales[compositeCaptureStep_];
                binding.component = kDirectionComponents[compositeCaptureStep_];
                compositeCaptured_[compositeCaptureStep_] = true;
                ++compositeCaptureStep_;
                if (compositeCaptureStep_ < 4) {
                    if (!inputService.BeginRebind(CompositeCaptureOptions())) {
                        compositeCaptureMessage_ = TEXT::Localize("ui.input.captureContinueFailed");
                        compositeCaptureStep_ = -1;
                        rebindOwner_ = RebindOwner::None;
                    }
                } else {
                    compositeCaptureMessage_ = TEXT::Localize("ui.input.allDirectionsCaptured");
                    compositeCaptureStep_ = -1;
                    rebindOwner_ = RebindOwner::None;
                }
            }
        } else if (status == INPUT::InputRebindStatus::Cancelled || status == INPUT::InputRebindStatus::TimedOut) {
            compositeCaptureMessage_ = status == INPUT::InputRebindStatus::Cancelled
                                           ? TEXT::Localize("ui.input.captureCancelledKept")
                                           : TEXT::Localize("ui.input.captureTimedOutKept");
            inputService.ResetRebind();
            compositeCaptureStep_ = -1;
            rebindOwner_ = RebindOwner::None;
        }
    }

    if (rebindOwner_ == RebindOwner::Composite && compositeCaptureStep_ >= 0 && compositeCaptureStep_ < 4) {
        const INPUT::InputRebindOperation& operation = inputService.GetRebindOperation();
        ImGui::BeginChild("##CompositeCaptureStatus", ImVec2(0.0f, 66.0f), true);
        if (operation.GetStatus() == INPUT::InputRebindStatus::WaitingForRelease) {
            ImGui::Text(TEXT::Localize("editor.ui.release.the.previous.control.then.press.s.623510").c_str(),
                        TEXT::Localize(kDirectionNameKeys[compositeCaptureStep_]).c_str());
        } else {
            ImGui::Text(TEXT::Localize("editor.ui.press.the.control.for.s.d6d830").c_str(),
                        TEXT::Localize(kDirectionNameKeys[compositeCaptureStep_]).c_str());
        }
        ImGui::TextDisabled(TEXT::Localize("ui.input.escapeCancelsFormat").c_str(), operation.GetRemainingSeconds());
        ImGui::SameLine();
        if (ImGui::SmallButton(TEXT::Localize("ui.input.cancelCapture").c_str())) {
            inputService.CancelRebind();
            inputService.ResetRebind();
            compositeCaptureStep_ = -1;
            rebindOwner_ = RebindOwner::None;
            compositeCaptureMessage_ = TEXT::Localize("ui.input.captureCancelledKept");
        }
        ImGui::EndChild();
    } else if (!compositeCaptureMessage_.empty()) {
        ImGui::TextWrapped("%s", compositeCaptureMessage_.c_str());
    }
#else
    (void)inputService;
#endif
}

void InputActionMapEditor::DrawCompositePopup(INPUT::InputService& inputService) {
#if defined(HIKARI_WITH_EDITOR)
    INPUT::InputActionMap& map = inputService.EditActionMap();
    if (compositePopupRequested_) {
        ImGui::OpenPopup((TEXT::Localize("ui.input.compositeTitle") + "###Axis2DCompositeBinding").c_str());
        compositePopupRequested_ = false;
    }
    if (!ImGui::BeginPopupModal((TEXT::Localize("ui.input.compositeTitle") + "###Axis2DCompositeBinding").c_str(),
                                nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextDisabled("%s", TEXT::Localize("ui.input.compositeHeader").c_str());
    ImGui::TextWrapped("%s", TEXT::Localize("ui.input.compositeIntro").c_str());
    ImGui::SetNextItemWidth(380.0f);
    if (ImGui::BeginCombo(TEXT::Localize("ui.input.action").c_str(),
                          compositeActionId_.empty() ? TEXT::Localize("ui.input.selectAxis2DAction").c_str()
                                                     : compositeActionId_.c_str())) {
        for (const INPUT::InputActionDefinition& action : map.actions) {
            if (action.valueType != INPUT::InputActionValueType::Axis2D)
                continue;
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
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (ImGui::BeginTable("CompositeDirections", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(TEXT::Localize("ui.input.tableDirection").c_str(), ImGuiTableColumnFlags_WidthFixed,
                                90.0f);
        ImGui::TableSetupColumn(TEXT::Localize("ui.input.tableCapturedControl").c_str());
        ImGui::TableSetupColumn(TEXT::Localize("ui.input.tableOutput").c_str(), ImGuiTableColumnFlags_WidthFixed,
                                80.0f);
        ImGui::TableHeadersRow();
        for (int index = 0; index < 4; ++index) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(TEXT::Localize(kDirectionNameKeys[index]).c_str());
            ImGui::TableSetColumnIndex(1);
            if (compositeCaptured_[index]) {
                ImGui::Text("%s / %s", INPUT::ToString(compositeBindings_[index].source),
                            compositeBindings_[index].control.c_str());
            } else if (compositeCaptureStep_ == index && rebindOwner_ == RebindOwner::Composite) {
                ImGui::TextColored(ImVec4(0.38f, 0.85f, 0.9f, 1.0f), "%s",
                                   TEXT::Localize("ui.input.listening").c_str());
            } else {
                ImGui::TextDisabled("%s", TEXT::Localize("ui.input.notCaptured").c_str());
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s %.0f", kDirectionComponents[index] == 0 ? "X" : "Y", kDirectionScales[index]);
        }
        ImGui::EndTable();
    }

    DrawCompositeCapture(inputService);
    const bool capturing = rebindOwner_ == RebindOwner::Composite;
    ImGui::BeginDisabled(capturing || compositeActionId_.empty());
    if (ImGui::Button(compositeCaptured_[0] ? TEXT::Localize("ui.input.restartCapture").c_str()
                                            : TEXT::Localize("ui.input.startCapture").c_str(),
                      ImVec2(140.0f, 0.0f))) {
        BeginCompositeCapture(inputService);
    }
    ImGui::EndDisabled();

    const bool allCaptured =
        std::all_of(compositeCaptured_.begin(), compositeCaptured_.end(), [](bool captured) { return captured; });
    bool duplicateDirections = false;
    if (allCaptured) {
        for (int lhs = 0; lhs < 4; ++lhs) {
            for (int rhs = lhs + 1; rhs < 4; ++rhs) {
                duplicateDirections =
                    duplicateDirections || SamePhysicalControl(compositeBindings_[lhs], compositeBindings_[rhs]);
            }
        }
    }

    std::unordered_set<int> conflictIndices;
    if (allCaptured && selectedContext_ >= 0 && selectedContext_ < static_cast<int>(map.contexts.size())) {
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
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.32f, 1.0f), "%s",
                           TEXT::Localize("ui.input.duplicateDirections").c_str());
    }
    if (!conflictIndices.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.68f, 0.25f, 1.0f),
                           TEXT::Localize("ui.input.existingBindingsUseFormat").c_str(), conflictIndices.size());
        ImGui::Checkbox(TEXT::Localize("ui.input.replaceConflicting").c_str(), &compositeReplaceConflicts_);
    }

    ImGui::Spacing();
    const bool canAdd = allCaptured && !duplicateDirections && selectedContext_ >= 0 &&
                        selectedContext_ < static_cast<int>(map.contexts.size());
    ImGui::BeginDisabled(!canAdd);
    if (ImGui::Button(TEXT::Localize("ui.input.addComposite").c_str(), ImVec2(140.0f, 0.0f))) {
        INPUT::InputContextDefinition& context = map.contexts[selectedContext_];
        if (compositeReplaceConflicts_ && !conflictIndices.empty()) {
            std::vector<int> ordered(conflictIndices.begin(), conflictIndices.end());
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
        SetStatus(TEXT::Localize("ui.input.compositeAdded"), false);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(TEXT::Localize("ui.common.cancel").c_str(), ImVec2(110.0f, 0.0f))) {
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
