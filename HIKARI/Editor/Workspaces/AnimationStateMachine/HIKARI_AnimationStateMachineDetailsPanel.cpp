#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/AnimationStateMachine/HIKARI_AnimationStateMachineWorkspaceController.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#include "imgui.h"

namespace HIKARI::EDITOR {
namespace {
bool InputTextString(const char* label, std::string& value) {
    std::array<char, 256> buffer{};
    const size_t copyCount = (std::min)(value.size(), buffer.size() - 1u);
    if (copyCount > 0u) {
        std::memcpy(buffer.data(), value.data(), copyCount);
    }
    if (!ImGui::InputText(label, buffer.data(), buffer.size())) {
        return false;
    }
    value = buffer.data();
    return true;
}

const char* ParameterSourceName(ANIMATION::AnimationParameterSource source) noexcept {
    using Source = ANIMATION::AnimationParameterSource;
    switch (source) {
    case Source::CharacterGrounded: return TEXT::Localize("editor.ui.character.grounded.4a238d").c_str();
    case Source::CharacterMoving: return TEXT::Localize("editor.ui.character.moving.ead4a7").c_str();
    case Source::CharacterHorizontalSpeed: return TEXT::Localize("editor.ui.horizontal.speed.1500fb").c_str();
    case Source::CharacterVerticalSpeed: return TEXT::Localize("editor.ui.vertical.speed.0cc532").c_str();
    case Source::CharacterInputMagnitude: return TEXT::Localize("editor.ui.input.magnitude.19185f").c_str();
    case Source::CharacterSprinting: return TEXT::Localize("editor.ui.character.sprinting.177b29").c_str();
    case Source::CharacterFalling: return TEXT::Localize("editor.ui.character.falling.39a12a").c_str();
    case Source::Manual:
    default: return TEXT::Localize("editor.ui.manual.c.0a8a40").c_str();
    }
}

ANIMATION::AnimationConditionOperator DefaultOperator(ANIMATION::AnimationParameterType type) noexcept {
    using Type = ANIMATION::AnimationParameterType;
    using Op = ANIMATION::AnimationConditionOperator;
    if (type == Type::Trigger)
        return Op::Triggered;
    if (type == Type::Bool)
        return Op::IsTrue;
    return Op::Greater;
}

const char* ConditionOperatorName(ANIMATION::AnimationConditionOperator value) noexcept {
    using Op = ANIMATION::AnimationConditionOperator;
    switch (value) {
    case Op::IsFalse: return TEXT::Localize("editor.ui.is.false.b63c27").c_str();
    case Op::Greater: return ">";
    case Op::GreaterOrEqual: return ">=";
    case Op::Less: return "<";
    case Op::LessOrEqual: return "<=";
    case Op::Equal: return "==";
    case Op::NotEqual: return "!=";
    case Op::Triggered: return TEXT::Localize("editor.ui.triggered.933f5e").c_str();
    case Op::IsTrue:
    default: return TEXT::Localize("editor.ui.is.true.905c77").c_str();
    }
}
} // namespace

void AnimationStateMachineWorkspaceController::DrawDetailsWindow(DocumentSceneBase& scene) {
    if (!ImGui::Begin(TEXT::LocalizeWindow("editor.ui.details.dc3dec", "AnimationSM/Details").c_str())) {
        ImGui::End();
        return;
    }
    auto& definition = document_.Definition();
    ImGui::SeparatorText(TEXT::Localize("editor.ui.state.machine.ff3372").c_str());
    {
        auto before = definition;
        if (InputTextString(TEXT::Localize("ui.input.name").c_str(), definition.name)) {
            RecordMutation(std::move(before), 1u);
        }
    }

    const std::vector<const AssetRecord*> models = scene.GetAssetDatabase().CollectByType(AssetType::Model);
    std::string modelPreview = "<select model>";
    if (!definition.previewModelAssetId.value.empty()) {
        const AssetRecord* selectedRecord =
            scene.GetAssetDatabase().FindByGuid(AssetGuid{definition.previewModelAssetId.value});
        modelPreview = selectedRecord != nullptr && !selectedRecord->displayName.empty()
                           ? selectedRecord->displayName
                           : definition.previewModelAssetId.value;
    }
    if (ImGui::BeginCombo(TEXT::LocalizeWindow("editor.ui.animation.model.a79a75", "Animation Model").c_str(),
                          modelPreview.c_str())) {
        for (const AssetRecord* record : models) {
            if (record == nullptr)
                continue;
            const bool selected = record->guid.value == definition.previewModelAssetId.value;
            const std::string label = record->displayName + "##Model" + record->guid.value;
            if (ImGui::Selectable(label.c_str(), selected)) {
                auto before = definition;
                definition.previewModelAssetId.value = record->guid.value;
                RecordMutation(std::move(before));
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SeparatorText(TEXT::Localize("editor.ui.selection.d1f8e5").c_str());
    if (selectionKind_ == SelectionKind::State) {
        ANIMATION::AnimationState* state = SelectedState();
        if (state != nullptr) {
            auto before = definition;
            if (InputTextString(TEXT::Localize("editor.ui.state.name.5a9dd2").c_str(), state->name)) {
                RecordMutation(std::move(before), 1000u + state->id.value);
                state = SelectedState();
            }
            bool entry = definition.entryStateId == state->id;
            if (ImGui::Checkbox(TEXT::LocalizeWindow("editor.ui.entry.state.9b7e45", "Entry State").c_str(), &entry) &&
                entry) {
                before = definition;
                definition.entryStateId = state->id;
                RecordMutation(std::move(before));
            }

            DrawSelectedStateMotionEditor(scene, *state);
            state = SelectedState();
            before = definition;
            if (ImGui::DragFloat(TEXT::LocalizeWindow("ui.vfx.text.Speed", "Speed").c_str(), &state->speed, 0.02f,
                                 -8.0f, 8.0f)) {
                RecordMutation(std::move(before), 2000u + state->id.value);
                state = SelectedState();
            }
            before = definition;
            if (ImGui::Checkbox(TEXT::LocalizeWindow("ui.vfx.text.Loop", "Loop").c_str(), &state->loop)) {
                RecordMutation(std::move(before));
            }
        }
    } else if (selectionKind_ == SelectionKind::Parameter) {
        ANIMATION::AnimationParameterDefinition* parameter = SelectedParameter();
        if (parameter != nullptr) {
            auto before = definition;
            if (InputTextString(TEXT::Localize("editor.ui.parameter.name.94d788").c_str(), parameter->name)) {
                RecordMutation(std::move(before), 3000u + parameter->id.value);
                parameter = SelectedParameter();
            }
            int type = static_cast<int>(parameter->type);
            const char* types[]{
                TEXT::Localize("editor.ui.bool.daf760").c_str(), TEXT::Localize("editor.ui.float.64435f").c_str(),
                TEXT::Localize("editor.ui.integer.9edcff").c_str(), TEXT::Localize("editor.ui.trigger.d3f06a").c_str()};
            before = definition;
            if (ImGui::Combo(TEXT::LocalizeWindow("ui.vfx.text.Type", "Type").c_str(), &type, types, 4)) {
                parameter->type = static_cast<ANIMATION::AnimationParameterType>(type);
                parameter->defaultValue = ANIMATION::MakeDefaultAnimationParameterValue(parameter->type);
                if (parameter->type == ANIMATION::AnimationParameterType::Trigger) {
                    parameter->source = ANIMATION::AnimationParameterSource::Manual;
                }
                RecordMutation(std::move(before));
                parameter = SelectedParameter();
            }
            int source = static_cast<int>(parameter->source);
            const char* sources[]{TEXT::Localize("editor.ui.manual.c.0a8a40").c_str(),
                                  TEXT::Localize("editor.ui.character.grounded.4a238d").c_str(),
                                  TEXT::Localize("editor.ui.character.moving.ead4a7").c_str(),
                                  TEXT::Localize("editor.ui.horizontal.speed.1500fb").c_str(),
                                  TEXT::Localize("editor.ui.vertical.speed.0cc532").c_str(),
                                  TEXT::Localize("editor.ui.input.magnitude.19185f").c_str(),
                                  TEXT::Localize("editor.ui.character.sprinting.177b29").c_str(),
                                  TEXT::Localize("editor.ui.character.falling.39a12a").c_str()};
            ImGui::BeginDisabled(parameter->type == ANIMATION::AnimationParameterType::Trigger);
            before = definition;
            if (ImGui::Combo(TEXT::LocalizeWindow("editor.ui.binding.source.35edd6", "Binding Source").c_str(), &source,
                             sources, 8)) {
                parameter->source = static_cast<ANIMATION::AnimationParameterSource>(source);
                RecordMutation(std::move(before));
                parameter = SelectedParameter();
            }
            ImGui::EndDisabled();
            ImGui::TextDisabled(TEXT::Localize("ui.assetBrowser.tooltipSource").c_str(),
                                ParameterSourceName(parameter->source));

            before = definition;
            if (parameter->type == ANIMATION::AnimationParameterType::Float) {
                float value = std::get<float>(parameter->defaultValue);
                if (ImGui::DragFloat(TEXT::LocalizeWindow("ui.common.default", "Default").c_str(), &value, 0.02f)) {
                    parameter->defaultValue = value;
                    RecordMutation(std::move(before), 4000u + parameter->id.value);
                }
            } else if (parameter->type == ANIMATION::AnimationParameterType::Integer) {
                int value = std::get<int32_t>(parameter->defaultValue);
                if (ImGui::DragInt(TEXT::LocalizeWindow("ui.common.default", "Default").c_str(), &value)) {
                    parameter->defaultValue = static_cast<int32_t>(value);
                    RecordMutation(std::move(before), 4000u + parameter->id.value);
                }
            } else if (parameter->type == ANIMATION::AnimationParameterType::Bool) {
                bool value = std::get<bool>(parameter->defaultValue);
                if (ImGui::Checkbox(TEXT::LocalizeWindow("ui.common.default", "Default").c_str(), &value)) {
                    parameter->defaultValue = value;
                    RecordMutation(std::move(before));
                }
            }
        }
    } else if (selectionKind_ == SelectionKind::Transition) {
        ANIMATION::AnimationStateTransition* transition = SelectedTransition();
        if (transition != nullptr) {
            auto before = definition;
            const auto* source = ANIMATION::FindAnimationState(definition, transition->sourceStateId);
            if (ImGui::BeginCombo(TEXT::LocalizeWindow("ui.modelDiagnostics.source", "Source").c_str(),
                                  source != nullptr ? source->name.c_str() : "Any State")) {
                const bool anySelected = !transition->sourceStateId.IsValid();
                if (ImGui::Selectable(TEXT::LocalizeWindow("editor.ui.any.state.4bcf51", "Any State").c_str(),
                                      anySelected)) {
                    before = definition;
                    transition->sourceStateId = {};
                    RecordMutation(std::move(before));
                    transition = SelectedTransition();
                }
                for (const auto& state : definition.states) {
                    const bool selected = state.id == transition->sourceStateId;
                    if (ImGui::Selectable(state.name.c_str(), selected)) {
                        before = definition;
                        transition->sourceStateId = state.id;
                        RecordMutation(std::move(before));
                        transition = SelectedTransition();
                    }
                }
                ImGui::EndCombo();
            }
            const auto* target = ANIMATION::FindAnimationState(definition, transition->targetStateId);
            if (ImGui::BeginCombo(TEXT::LocalizeWindow("editor.ui.target.61ad50", "Target").c_str(),
                                  target != nullptr ? target->name.c_str() : "<missing>")) {
                for (const auto& state : definition.states) {
                    const bool selected = state.id == transition->targetStateId;
                    if (ImGui::Selectable(state.name.c_str(), selected)) {
                        before = definition;
                        transition->targetStateId = state.id;
                        RecordMutation(std::move(before));
                        transition = SelectedTransition();
                    }
                }
                ImGui::EndCombo();
            }
            before = definition;
            if (ImGui::DragInt(TEXT::LocalizeWindow("ui.input.priority", "Priority").c_str(), &transition->priority,
                               1.0f)) {
                RecordMutation(std::move(before), 5000u + transition->id.value);
                transition = SelectedTransition();
            }
            before = definition;
            if (ImGui::DragFloat(TEXT::LocalizeWindow("editor.ui.blend.duration.c087cd", "Blend Duration").c_str(),
                                 &transition->blendDurationSec, 0.01f, 0.0f, 10.0f, "%.2f s")) {
                RecordMutation(std::move(before), 6000u + transition->id.value);
                transition = SelectedTransition();
            }
            before = definition;
            if (ImGui::Checkbox(TEXT::LocalizeWindow("editor.ui.require.exit.time.048813", "Require Exit Time").c_str(),
                                &transition->requireExitTime)) {
                RecordMutation(std::move(before));
                transition = SelectedTransition();
            }
            if (transition->requireExitTime) {
                before = definition;
                if (ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.exit.time.2d3d87", "Exit Time").c_str(),
                                       &transition->exitTimeNormalized, 0.0f, 1.0f)) {
                    RecordMutation(std::move(before), 7000u + transition->id.value);
                    transition = SelectedTransition();
                }
            }
            before = definition;
            if (ImGui::Checkbox(
                    TEXT::LocalizeWindow("editor.ui.allow.self.transition.031ff2", "Allow Self Transition").c_str(),
                    &transition->allowSelfTransition)) {
                RecordMutation(std::move(before));
                transition = SelectedTransition();
            }

            ImGui::SeparatorText(TEXT::Localize("editor.ui.conditions.all.must.pass.62a20e").c_str());
            ImGui::BeginDisabled(definition.parameters.empty());
            if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.condition.e937dd", "+ Condition").c_str())) {
                before = definition;
                const auto& parameter = definition.parameters.front();
                transition->conditions.push_back({parameter.id, DefaultOperator(parameter.type),
                                                  ANIMATION::MakeDefaultAnimationParameterValue(parameter.type)});
                RecordMutation(std::move(before));
                transition = SelectedTransition();
            }
            ImGui::EndDisabled();
            for (size_t index = 0u; index < transition->conditions.size();) {
                ImGui::PushID(static_cast<int>(index));
                auto& condition = transition->conditions[index];
                auto* parameter = ANIMATION::FindAnimationParameter(definition, condition.parameterId);
                const char* parameterPreview = parameter != nullptr ? parameter->name.c_str() : "<missing>";
                if (ImGui::BeginCombo(TEXT::LocalizeWindow("editor.ui.parameter.f699f2", "Parameter").c_str(),
                                      parameterPreview)) {
                    for (const auto& candidate : definition.parameters) {
                        const bool selected = candidate.id == condition.parameterId;
                        if (ImGui::Selectable(candidate.name.c_str(), selected)) {
                            before = definition;
                            condition.parameterId = candidate.id;
                            condition.comparison = DefaultOperator(candidate.type);
                            condition.threshold = ANIMATION::MakeDefaultAnimationParameterValue(candidate.type);
                            RecordMutation(std::move(before));
                            transition = SelectedTransition();
                            parameter = ANIMATION::FindAnimationParameter(definition,
                                                                          transition->conditions[index].parameterId);
                        }
                    }
                    ImGui::EndCombo();
                }
                if (parameter != nullptr) {
                    using Type = ANIMATION::AnimationParameterType;
                    using Op = ANIMATION::AnimationConditionOperator;
                    const std::array<Op, 6> numericOps{Op::Greater,     Op::GreaterOrEqual, Op::Less,
                                                       Op::LessOrEqual, Op::Equal,          Op::NotEqual};
                    const std::array<Op, 4> boolOps{Op::IsTrue, Op::IsFalse, Op::Equal, Op::NotEqual};
                    if (parameter->type == Type::Trigger) {
                        ImGui::TextUnformatted(TEXT::Localize("editor.ui.triggered.933f5e").c_str());
                    } else if (ImGui::BeginCombo(
                                   TEXT::LocalizeWindow("editor.ui.comparison.2dfcf8", "Comparison").c_str(),
                                   ConditionOperatorName(condition.comparison))) {
                        if (parameter->type == Type::Bool) {
                            for (const Op value : boolOps) {
                                if (ImGui::Selectable(ConditionOperatorName(value), value == condition.comparison)) {
                                    before = definition;
                                    condition.comparison = value;
                                    RecordMutation(std::move(before));
                                    transition = SelectedTransition();
                                }
                            }
                        } else {
                            for (const Op value : numericOps) {
                                if (ImGui::Selectable(ConditionOperatorName(value), value == condition.comparison)) {
                                    before = definition;
                                    condition.comparison = value;
                                    RecordMutation(std::move(before));
                                    transition = SelectedTransition();
                                }
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (parameter->type == Type::Float) {
                        float value = std::get<float>(condition.threshold);
                        if (ImGui::DragFloat(TEXT::LocalizeWindow("ui.quality.threshold", "Threshold").c_str(), &value,
                                             0.02f)) {
                            before = definition;
                            condition.threshold = value;
                            RecordMutation(std::move(before),
                                           8000u + transition->id.value * 32u + static_cast<uint64_t>(index));
                            transition = SelectedTransition();
                        }
                    } else if (parameter->type == Type::Integer) {
                        int value = std::get<int32_t>(condition.threshold);
                        if (ImGui::DragInt(TEXT::LocalizeWindow("ui.quality.threshold", "Threshold").c_str(), &value)) {
                            before = definition;
                            condition.threshold = static_cast<int32_t>(value);
                            RecordMutation(std::move(before),
                                           8000u + transition->id.value * 32u + static_cast<uint64_t>(index));
                            transition = SelectedTransition();
                        }
                    } else if (parameter->type == Type::Bool &&
                               (condition.comparison == Op::Equal || condition.comparison == Op::NotEqual)) {
                        bool value = std::get<bool>(condition.threshold);
                        if (ImGui::Checkbox(TEXT::LocalizeWindow("editor.ui.expected.6ac87f", "Expected").c_str(),
                                            &value)) {
                            before = definition;
                            condition.threshold = value;
                            RecordMutation(std::move(before));
                            transition = SelectedTransition();
                        }
                    }
                }
                ImGui::SameLine();
                bool removed = false;
                if (ImGui::SmallButton("X")) {
                    before = definition;
                    transition->conditions.erase(transition->conditions.begin() + index);
                    RecordMutation(std::move(before));
                    transition = SelectedTransition();
                    removed = true;
                }
                ImGui::Separator();
                ImGui::PopID();
                if (!removed)
                    ++index;
            }
        }
    } else {
        ImGui::TextDisabled(
            TEXT::Localize("editor.ui.select.a.state.parameter.or.transition.to.edit.it.7a9fef").c_str());
    }
    ImGui::End();
}

} // namespace HIKARI::EDITOR
