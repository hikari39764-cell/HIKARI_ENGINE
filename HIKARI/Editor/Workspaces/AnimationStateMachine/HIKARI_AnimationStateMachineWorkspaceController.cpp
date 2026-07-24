#include "Editor/Workspaces/AnimationStateMachine/HIKARI_AnimationStateMachineWorkspaceController.h"

#include <algorithm>
#include <utility>

#include "Assets/Animation/HIKARI_AnimationStateMachineAssetStore.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

void AnimationStateMachineWorkspaceController::ApplyWorkspaceActivation(
    DocumentSceneBase &scene, const EditorWorkspaceActivation &activation) {
  if (activation.current != EditorWorkspaceId::AnimationStateMachine) {
    return;
  }
  if (activation.animationStateMachineAssetGuid) {
    if (!document_.Open(scene.GetAssetDatabase(),
                        *activation.animationStateMachineAssetGuid,
                        statusMessage_)) {
      return;
    }
  } else if (!document_.IsOpen()) {
    document_.New();
    statusMessage_ = "New animation state machine";
  }
  selectionKind_ = SelectionKind::State;
  selectedStateId_ = document_.Definition().entryStateId;
  selectedParameterId_ = {};
  selectedTransitionId_ = {};
  NormalizeSelection();
}

bool AnimationStateMachineWorkspaceController::IsDocumentOpen() const noexcept {
  return document_.IsOpen();
}

bool AnimationStateMachineWorkspaceController::IsDocumentDirty()
    const noexcept {
  return document_.IsDirty();
}

bool AnimationStateMachineWorkspaceController::CanUndo() const noexcept {
  return document_.CanUndo();
}

bool AnimationStateMachineWorkspaceController::CanRedo() const noexcept {
  return document_.CanRedo();
}

bool AnimationStateMachineWorkspaceController::Save(DocumentSceneBase &scene,
                                                    std::string &outMessage) {
  const bool saved = document_.Save(scene.GetAssetDatabase(), outMessage);
  if (saved) {
    if (auto *store = scene.GetWorld()
                          .Services()
                          .Find<AnimationStateMachineAssetStore>()) {
      store->Invalidate(document_.GetAssetGuid());
    }
  }
  return saved;
}

bool AnimationStateMachineWorkspaceController::Undo(std::string &outMessage) {
  if (!document_.Undo()) {
    outMessage = "Nothing to undo";
    return false;
  }
  NormalizeSelection();
  outMessage = "Animation state machine edit undone";
  return true;
}

bool AnimationStateMachineWorkspaceController::Redo(std::string &outMessage) {
  if (!document_.Redo()) {
    outMessage = "Nothing to redo";
    return false;
  }
  NormalizeSelection();
  outMessage = "Animation state machine edit redone";
  return true;
}

void AnimationStateMachineWorkspaceController::BindDocumentCommands(
    EditorCommandRouter &commandRouter, DocumentSceneBase &scene) {

  commandRouter.Bind(EditorCommandId::SaveDocument,
                     "Save Animation State Machine", IsDocumentOpen(),
                     [this, &scene]() { (void)Save(scene, statusMessage_); });
  commandRouter.Bind(EditorCommandId::Undo, "Undo Animation State Machine Edit",
                     CanUndo(), [this]() { (void)Undo(statusMessage_); });
  commandRouter.Bind(EditorCommandId::Redo, "Redo Animation State Machine Edit",
                     CanRedo(), [this]() { (void)Redo(statusMessage_); });
}

void AnimationStateMachineWorkspaceController::AddState() {
  const auto &definition = document_.Definition();
  AddStateAt(
      100.0f + static_cast<float>(definition.states.size() % 4u) * 190.0f,
      100.0f + static_cast<float>(definition.states.size() / 4u) * 120.0f);
}

void AnimationStateMachineWorkspaceController::AddStateAt(float graphX,
                                                          float graphY) {
  auto before = document_.Definition();
  auto &definition = document_.Definition();
  ANIMATION::AnimationState state{};
  state.id = ANIMATION::AllocateAnimationStateId(definition);
  state.name = "State";
  state.editorX = graphX;
  state.editorY = graphY;
  definition.states.push_back(std::move(state));
  ANIMATION::NormalizeAnimationStateMachine(definition);
  selectedStateId_ = definition.states.back().id;
  selectionKind_ = SelectionKind::State;
  RecordMutation(std::move(before));
}

void AnimationStateMachineWorkspaceController::DeleteSelectedState() {
  auto &definition = document_.Definition();
  if (definition.states.size() <= 1u || !selectedStateId_.IsValid()) {
    statusMessage_ = "A state machine must keep at least one state";
    return;
  }
  auto before = definition;
  definition.states.erase(
      std::remove_if(definition.states.begin(), definition.states.end(),
                     [this](const ANIMATION::AnimationState &state) {
                       return state.id == selectedStateId_;
                     }),
      definition.states.end());
  definition.transitions.erase(
      std::remove_if(
          definition.transitions.begin(), definition.transitions.end(),
          [this](const ANIMATION::AnimationStateTransition &transition) {
            return transition.sourceStateId == selectedStateId_ ||
                   transition.targetStateId == selectedStateId_;
          }),
      definition.transitions.end());
  if (definition.entryStateId == selectedStateId_) {
    definition.entryStateId = definition.states.front().id;
  }
  selectedStateId_ = definition.entryStateId;
  RecordMutation(std::move(before));
}

void AnimationStateMachineWorkspaceController::AddParameter(
    ANIMATION::AnimationParameterType type) {
  auto before = document_.Definition();
  auto &definition = document_.Definition();
  ANIMATION::AnimationParameterDefinition parameter{};
  parameter.id = ANIMATION::AllocateAnimationParameterId(definition);
  parameter.name = type == ANIMATION::AnimationParameterType::Trigger
                       ? "Trigger"
                       : "Parameter";
  parameter.type = type;
  parameter.defaultValue = ANIMATION::MakeDefaultAnimationParameterValue(type);
  definition.parameters.push_back(std::move(parameter));
  ANIMATION::NormalizeAnimationStateMachine(definition);
  selectedParameterId_ = definition.parameters.back().id;
  selectionKind_ = SelectionKind::Parameter;
  RecordMutation(std::move(before));
}

void AnimationStateMachineWorkspaceController::DeleteSelectedParameter() {
  if (!selectedParameterId_.IsValid())
    return;
  auto before = document_.Definition();
  auto &definition = document_.Definition();
  definition.parameters.erase(
      std::remove_if(
          definition.parameters.begin(), definition.parameters.end(),
          [this](const ANIMATION::AnimationParameterDefinition &value) {
            return value.id == selectedParameterId_;
          }),
      definition.parameters.end());
  for (auto &transition : definition.transitions) {
    transition.conditions.erase(
        std::remove_if(
            transition.conditions.begin(), transition.conditions.end(),
            [this](const ANIMATION::AnimationTransitionCondition &value) {
              return value.parameterId == selectedParameterId_;
            }),
        transition.conditions.end());
  }
  for (auto &state : definition.states) {
    auto *blendTree =
        std::get_if<ANIMATION::AnimationBlendTree1DMotion>(&state.motion);
    if (blendTree != nullptr &&
        blendTree->parameterId == selectedParameterId_) {
      blendTree->parameterId = {};
    }
  }
  selectedParameterId_ = {};
  selectionKind_ = SelectionKind::None;
  RecordMutation(std::move(before));
}

void AnimationStateMachineWorkspaceController::AddTransition() {
  auto &definition = document_.Definition();
  if (definition.states.empty())
    return;
  auto before = definition;
  ANIMATION::AnimationStateTransition transition{};
  transition.id = ANIMATION::AllocateAnimationTransitionId(definition);
  transition.sourceStateId =
      selectedStateId_.IsValid() ? selectedStateId_ : definition.entryStateId;
  transition.targetStateId = definition.states.front().id;
  for (const auto &state : definition.states) {
    if (state.id != transition.sourceStateId) {
      transition.targetStateId = state.id;
      break;
    }
  }
  const ANIMATION::AnimationTransitionId transitionId = transition.id;
  definition.transitions.push_back(std::move(transition));
  ANIMATION::NormalizeAnimationStateMachine(definition);
  selectedTransitionId_ = transitionId;
  selectionKind_ = SelectionKind::Transition;
  RecordMutation(std::move(before));
}

void AnimationStateMachineWorkspaceController::AddTransitionBetween(
    ANIMATION::AnimationStateId source, ANIMATION::AnimationStateId target) {
  auto &definition = document_.Definition();
  if (!source.IsValid() || !target.IsValid() || source == target ||
      ANIMATION::FindAnimationState(definition, source) == nullptr ||
      ANIMATION::FindAnimationState(definition, target) == nullptr) {
    return;
  }
  auto before = definition;
  ANIMATION::AnimationStateTransition transition{};
  transition.id = ANIMATION::AllocateAnimationTransitionId(definition);
  transition.sourceStateId = source;
  transition.targetStateId = target;
  selectedTransitionId_ = transition.id;
  definition.transitions.push_back(std::move(transition));
  selectionKind_ = SelectionKind::Transition;
  RecordMutation(std::move(before));
}

void AnimationStateMachineWorkspaceController::DeleteSelectedTransition() {
  if (!selectedTransitionId_.IsValid())
    return;
  auto before = document_.Definition();
  auto &transitions = document_.Definition().transitions;
  transitions.erase(
      std::remove_if(transitions.begin(), transitions.end(),
                     [this](const ANIMATION::AnimationStateTransition &value) {
                       return value.id == selectedTransitionId_;
                     }),
      transitions.end());
  selectedTransitionId_ = {};
  selectionKind_ = SelectionKind::None;
  RecordMutation(std::move(before));
}

void AnimationStateMachineWorkspaceController::NormalizeSelection() noexcept {
  const auto &definition = document_.Definition();
  if (selectionKind_ == SelectionKind::State &&
      ANIMATION::FindAnimationState(definition, selectedStateId_) == nullptr) {
    selectedStateId_ = definition.entryStateId;
  }
  if (selectionKind_ == SelectionKind::Parameter &&
      ANIMATION::FindAnimationParameter(definition, selectedParameterId_) ==
          nullptr) {
    selectionKind_ = SelectionKind::None;
    selectedParameterId_ = {};
  }
  if (selectionKind_ == SelectionKind::Transition) {
    const auto found = std::find_if(
        definition.transitions.begin(), definition.transitions.end(),
        [this](const ANIMATION::AnimationStateTransition &value) {
          return value.id == selectedTransitionId_;
        });
    if (found == definition.transitions.end()) {
      selectionKind_ = SelectionKind::None;
      selectedTransitionId_ = {};
    }
  }
}

void AnimationStateMachineWorkspaceController::RecordMutation(
    ANIMATION::AnimationStateMachineDefinition before, uint64_t mergeGroup) {
  ANIMATION::NormalizeAnimationStateMachine(document_.Definition());
  document_.RecordApplied(std::move(before), mergeGroup);
}

ANIMATION::AnimationState *
AnimationStateMachineWorkspaceController::SelectedState() noexcept {
  return ANIMATION::FindAnimationState(document_.Definition(),
                                       selectedStateId_);
}

ANIMATION::AnimationParameterDefinition *
AnimationStateMachineWorkspaceController::SelectedParameter() noexcept {
  return ANIMATION::FindAnimationParameter(document_.Definition(),
                                           selectedParameterId_);
}

ANIMATION::AnimationStateTransition *
AnimationStateMachineWorkspaceController::SelectedTransition() noexcept {
  auto &transitions = document_.Definition().transitions;
  const auto found =
      std::find_if(transitions.begin(), transitions.end(),
                   [this](const ANIMATION::AnimationStateTransition &value) {
                     return value.id == selectedTransitionId_;
                   });
  return found != transitions.end() ? &*found : nullptr;
}

} // namespace HIKARI::EDITOR
