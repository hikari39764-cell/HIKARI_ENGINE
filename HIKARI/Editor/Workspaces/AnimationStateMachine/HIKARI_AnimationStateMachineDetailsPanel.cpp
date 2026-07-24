#include "Editor/Workspaces/AnimationStateMachine/HIKARI_AnimationStateMachineWorkspaceController.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {
#if defined(HIKARI_WITH_EDITOR)
bool InputTextString(const char *label, std::string &value) {
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

const char *
ParameterSourceName(ANIMATION::AnimationParameterSource source) noexcept {
  using Source = ANIMATION::AnimationParameterSource;
  switch (source) {
  case Source::CharacterGrounded:
    return "Character Grounded";
  case Source::CharacterMoving:
    return "Character Moving";
  case Source::CharacterHorizontalSpeed:
    return "Horizontal Speed";
  case Source::CharacterVerticalSpeed:
    return "Vertical Speed";
  case Source::CharacterInputMagnitude:
    return "Input Magnitude";
  case Source::CharacterSprinting:
    return "Character Sprinting";
  case Source::CharacterFalling:
    return "Character Falling";
  case Source::Manual:
  default:
    return "Manual (C++)";
  }
}

ANIMATION::AnimationConditionOperator
DefaultOperator(ANIMATION::AnimationParameterType type) noexcept {
  using Type = ANIMATION::AnimationParameterType;
  using Op = ANIMATION::AnimationConditionOperator;
  if (type == Type::Trigger)
    return Op::Triggered;
  if (type == Type::Bool)
    return Op::IsTrue;
  return Op::Greater;
}

const char *
ConditionOperatorName(ANIMATION::AnimationConditionOperator value) noexcept {
  using Op = ANIMATION::AnimationConditionOperator;
  switch (value) {
  case Op::IsFalse:
    return "Is False";
  case Op::Greater:
    return ">";
  case Op::GreaterOrEqual:
    return ">=";
  case Op::Less:
    return "<";
  case Op::LessOrEqual:
    return "<=";
  case Op::Equal:
    return "==";
  case Op::NotEqual:
    return "!=";
  case Op::Triggered:
    return "Triggered";
  case Op::IsTrue:
  default:
    return "Is True";
  }
}
#endif
} // namespace

void AnimationStateMachineWorkspaceController::DrawDetailsWindow(
    DocumentSceneBase &scene) {
#if defined(HIKARI_WITH_EDITOR)
  if (!ImGui::Begin("Details###AnimationSM/Details")) {
    ImGui::End();
    return;
  }
  auto &definition = document_.Definition();
  ImGui::SeparatorText("State Machine");
  {
    auto before = definition;
    if (InputTextString("Name", definition.name)) {
      RecordMutation(std::move(before), 1u);
    }
  }

  const std::vector<const AssetRecord *> models =
      scene.GetAssetDatabase().CollectByType(AssetType::Model);
  std::string modelPreview = "<select model>";
  if (!definition.previewModelAssetId.value.empty()) {
    const AssetRecord *selectedRecord = scene.GetAssetDatabase().FindByGuid(
        AssetGuid{definition.previewModelAssetId.value});
    modelPreview =
        selectedRecord != nullptr && !selectedRecord->displayName.empty()
            ? selectedRecord->displayName
            : definition.previewModelAssetId.value;
  }
  if (ImGui::BeginCombo("Animation Model", modelPreview.c_str())) {
    for (const AssetRecord *record : models) {
      if (record == nullptr)
        continue;
      const bool selected =
          record->guid.value == definition.previewModelAssetId.value;
      const std::string label =
          record->displayName + "##Model" + record->guid.value;
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

  ImGui::SeparatorText("Selection");
  if (selectionKind_ == SelectionKind::State) {
    ANIMATION::AnimationState *state = SelectedState();
    if (state != nullptr) {
      auto before = definition;
      if (InputTextString("State Name", state->name)) {
        RecordMutation(std::move(before), 1000u + state->id.value);
        state = SelectedState();
      }
      bool entry = definition.entryStateId == state->id;
      if (ImGui::Checkbox("Entry State", &entry) && entry) {
        before = definition;
        definition.entryStateId = state->id;
        RecordMutation(std::move(before));
      }

      DrawSelectedStateMotionEditor(scene, *state);
      state = SelectedState();
      before = definition;
      if (ImGui::DragFloat("Speed", &state->speed, 0.02f, -8.0f, 8.0f)) {
        RecordMutation(std::move(before), 2000u + state->id.value);
        state = SelectedState();
      }
      before = definition;
      if (ImGui::Checkbox("Loop", &state->loop)) {
        RecordMutation(std::move(before));
      }
    }
  } else if (selectionKind_ == SelectionKind::Parameter) {
    ANIMATION::AnimationParameterDefinition *parameter = SelectedParameter();
    if (parameter != nullptr) {
      auto before = definition;
      if (InputTextString("Parameter Name", parameter->name)) {
        RecordMutation(std::move(before), 3000u + parameter->id.value);
        parameter = SelectedParameter();
      }
      int type = static_cast<int>(parameter->type);
      constexpr const char *types[]{"Bool", "Float", "Integer", "Trigger"};
      before = definition;
      if (ImGui::Combo("Type", &type, types, 4)) {
        parameter->type = static_cast<ANIMATION::AnimationParameterType>(type);
        parameter->defaultValue =
            ANIMATION::MakeDefaultAnimationParameterValue(parameter->type);
        if (parameter->type == ANIMATION::AnimationParameterType::Trigger) {
          parameter->source = ANIMATION::AnimationParameterSource::Manual;
        }
        RecordMutation(std::move(before));
        parameter = SelectedParameter();
      }
      int source = static_cast<int>(parameter->source);
      constexpr const char *sources[]{
          "Manual (C++)",        "Character Grounded", "Character Moving",
          "Horizontal Speed",    "Vertical Speed",     "Input Magnitude",
          "Character Sprinting", "Character Falling"};
      ImGui::BeginDisabled(parameter->type ==
                           ANIMATION::AnimationParameterType::Trigger);
      before = definition;
      if (ImGui::Combo("Binding Source", &source, sources, 8)) {
        parameter->source =
            static_cast<ANIMATION::AnimationParameterSource>(source);
        RecordMutation(std::move(before));
        parameter = SelectedParameter();
      }
      ImGui::EndDisabled();
      ImGui::TextDisabled("Source: %s", ParameterSourceName(parameter->source));

      before = definition;
      if (parameter->type == ANIMATION::AnimationParameterType::Float) {
        float value = std::get<float>(parameter->defaultValue);
        if (ImGui::DragFloat("Default", &value, 0.02f)) {
          parameter->defaultValue = value;
          RecordMutation(std::move(before), 4000u + parameter->id.value);
        }
      } else if (parameter->type ==
                 ANIMATION::AnimationParameterType::Integer) {
        int value = std::get<int32_t>(parameter->defaultValue);
        if (ImGui::DragInt("Default", &value)) {
          parameter->defaultValue = static_cast<int32_t>(value);
          RecordMutation(std::move(before), 4000u + parameter->id.value);
        }
      } else if (parameter->type == ANIMATION::AnimationParameterType::Bool) {
        bool value = std::get<bool>(parameter->defaultValue);
        if (ImGui::Checkbox("Default", &value)) {
          parameter->defaultValue = value;
          RecordMutation(std::move(before));
        }
      }
    }
  } else if (selectionKind_ == SelectionKind::Transition) {
    ANIMATION::AnimationStateTransition *transition = SelectedTransition();
    if (transition != nullptr) {
      auto before = definition;
      const auto *source =
          ANIMATION::FindAnimationState(definition, transition->sourceStateId);
      if (ImGui::BeginCombo("Source", source != nullptr ? source->name.c_str()
                                                        : "Any State")) {
        const bool anySelected = !transition->sourceStateId.IsValid();
        if (ImGui::Selectable("Any State", anySelected)) {
          before = definition;
          transition->sourceStateId = {};
          RecordMutation(std::move(before));
          transition = SelectedTransition();
        }
        for (const auto &state : definition.states) {
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
      const auto *target =
          ANIMATION::FindAnimationState(definition, transition->targetStateId);
      if (ImGui::BeginCombo("Target", target != nullptr ? target->name.c_str()
                                                        : "<missing>")) {
        for (const auto &state : definition.states) {
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
      if (ImGui::DragInt("Priority", &transition->priority, 1.0f)) {
        RecordMutation(std::move(before), 5000u + transition->id.value);
        transition = SelectedTransition();
      }
      before = definition;
      if (ImGui::DragFloat("Blend Duration", &transition->blendDurationSec,
                           0.01f, 0.0f, 10.0f, "%.2f s")) {
        RecordMutation(std::move(before), 6000u + transition->id.value);
        transition = SelectedTransition();
      }
      before = definition;
      if (ImGui::Checkbox("Require Exit Time", &transition->requireExitTime)) {
        RecordMutation(std::move(before));
        transition = SelectedTransition();
      }
      if (transition->requireExitTime) {
        before = definition;
        if (ImGui::SliderFloat("Exit Time", &transition->exitTimeNormalized,
                               0.0f, 1.0f)) {
          RecordMutation(std::move(before), 7000u + transition->id.value);
          transition = SelectedTransition();
        }
      }
      before = definition;
      if (ImGui::Checkbox("Allow Self Transition",
                          &transition->allowSelfTransition)) {
        RecordMutation(std::move(before));
        transition = SelectedTransition();
      }

      ImGui::SeparatorText("Conditions (all must pass)");
      ImGui::BeginDisabled(definition.parameters.empty());
      if (ImGui::Button("+ Condition")) {
        before = definition;
        const auto &parameter = definition.parameters.front();
        transition->conditions.push_back(
            {parameter.id, DefaultOperator(parameter.type),
             ANIMATION::MakeDefaultAnimationParameterValue(parameter.type)});
        RecordMutation(std::move(before));
        transition = SelectedTransition();
      }
      ImGui::EndDisabled();
      for (size_t index = 0u; index < transition->conditions.size();) {
        ImGui::PushID(static_cast<int>(index));
        auto &condition = transition->conditions[index];
        auto *parameter = ANIMATION::FindAnimationParameter(
            definition, condition.parameterId);
        const char *parameterPreview =
            parameter != nullptr ? parameter->name.c_str() : "<missing>";
        if (ImGui::BeginCombo("Parameter", parameterPreview)) {
          for (const auto &candidate : definition.parameters) {
            const bool selected = candidate.id == condition.parameterId;
            if (ImGui::Selectable(candidate.name.c_str(), selected)) {
              before = definition;
              condition.parameterId = candidate.id;
              condition.comparison = DefaultOperator(candidate.type);
              condition.threshold =
                  ANIMATION::MakeDefaultAnimationParameterValue(candidate.type);
              RecordMutation(std::move(before));
              transition = SelectedTransition();
              parameter = ANIMATION::FindAnimationParameter(
                  definition, transition->conditions[index].parameterId);
            }
          }
          ImGui::EndCombo();
        }
        if (parameter != nullptr) {
          using Type = ANIMATION::AnimationParameterType;
          using Op = ANIMATION::AnimationConditionOperator;
          const std::array<Op, 6> numericOps{Op::Greater, Op::GreaterOrEqual,
                                             Op::Less,    Op::LessOrEqual,
                                             Op::Equal,   Op::NotEqual};
          const std::array<Op, 4> boolOps{Op::IsTrue, Op::IsFalse, Op::Equal,
                                          Op::NotEqual};
          if (parameter->type == Type::Trigger) {
            ImGui::TextUnformatted("Triggered");
          } else if (ImGui::BeginCombo(
                         "Comparison",
                         ConditionOperatorName(condition.comparison))) {
            if (parameter->type == Type::Bool) {
              for (const Op value : boolOps) {
                if (ImGui::Selectable(ConditionOperatorName(value),
                                      value == condition.comparison)) {
                  before = definition;
                  condition.comparison = value;
                  RecordMutation(std::move(before));
                  transition = SelectedTransition();
                }
              }
            } else {
              for (const Op value : numericOps) {
                if (ImGui::Selectable(ConditionOperatorName(value),
                                      value == condition.comparison)) {
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
            if (ImGui::DragFloat("Threshold", &value, 0.02f)) {
              before = definition;
              condition.threshold = value;
              RecordMutation(std::move(before),
                             8000u + transition->id.value * 32u +
                                 static_cast<uint64_t>(index));
              transition = SelectedTransition();
            }
          } else if (parameter->type == Type::Integer) {
            int value = std::get<int32_t>(condition.threshold);
            if (ImGui::DragInt("Threshold", &value)) {
              before = definition;
              condition.threshold = static_cast<int32_t>(value);
              RecordMutation(std::move(before),
                             8000u + transition->id.value * 32u +
                                 static_cast<uint64_t>(index));
              transition = SelectedTransition();
            }
          } else if (parameter->type == Type::Bool &&
                     (condition.comparison == Op::Equal ||
                      condition.comparison == Op::NotEqual)) {
            bool value = std::get<bool>(condition.threshold);
            if (ImGui::Checkbox("Expected", &value)) {
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
    ImGui::TextDisabled("Select a state, parameter, or transition to edit it.");
  }
  ImGui::End();
#else
  (void)scene;
#endif
}

} // namespace HIKARI::EDITOR
