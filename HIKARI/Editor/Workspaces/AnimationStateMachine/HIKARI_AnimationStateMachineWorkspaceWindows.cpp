#include "Editor/Workspaces/AnimationStateMachine/HIKARI_AnimationStateMachineWorkspaceController.h"

#include <string>
#include <utility>

#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

namespace HIKARI::EDITOR {
namespace {
#if defined(HIKARI_WITH_EDITOR)
const char *ParameterTypeName(ANIMATION::AnimationParameterType type) noexcept {
  switch (type) {
  case ANIMATION::AnimationParameterType::Float:
    return "Float";
  case ANIMATION::AnimationParameterType::Integer:
    return "Integer";
  case ANIMATION::AnimationParameterType::Trigger:
    return "Trigger";
  case ANIMATION::AnimationParameterType::Bool:
  default:
    return "Bool";
  }
}
#endif
} // namespace

void AnimationStateMachineWorkspaceController::DrawDockSpace(
    bool resetDefaultDockLayout) const {
#if defined(HIKARI_WITH_EDITOR)
  ImGuiIO &io = ImGui::GetIO();
  if ((io.ConfigFlags & ImGuiConfigFlags_DockingEnable) == 0)
    return;
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const ImGuiID dockspaceId =
      ImGui::GetID("HIKARI_AnimationStateMachineDockSpace_v2");
  if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr ||
      resetDefaultDockLayout) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);
    ImGuiID center = dockspaceId;
    ImGuiID left = 0u;
    ImGuiID right = 0u;
    ImGuiID leftBottom = 0u;
    ImGuiID bottom = 0u;
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, &left, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, &right, &center);
    ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.44f, &leftBottom, &left);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.13f, &bottom, &center);
    ImGui::DockBuilderDockWindow("State Graph###AnimationSM/Graph", center);
    ImGui::DockBuilderDockWindow("Parameters###AnimationSM/Parameters", left);
    ImGui::DockBuilderDockWindow("Transitions###AnimationSM/Transitions",
                                 leftBottom);
    ImGui::DockBuilderDockWindow("Details###AnimationSM/Details", right);
    ImGui::DockBuilderDockWindow("Diagnostics###AnimationSM/Diagnostics",
                                 bottom);
    ImGui::DockBuilderFinish(dockspaceId);
  }
  ImGui::DockSpaceOverViewport(dockspaceId, viewport);
#else
  (void)resetDefaultDockLayout;
#endif
}

AnimationStateMachineWorkspaceResult
AnimationStateMachineWorkspaceController::Draw(
    DocumentSceneBase &scene, EditorCommandRouter &commandRouter) {
  AnimationStateMachineWorkspaceResult result{};
#if defined(HIKARI_WITH_EDITOR)
  if (!document_.IsOpen())
    document_.New();
  DrawGraphWindow(scene, result, commandRouter);
  DrawParametersWindow();
  DrawTransitionsWindow();
  DrawDetailsWindow(scene);
  DrawDiagnosticsWindow(scene);
#else
  (void)scene;
  (void)commandRouter;
#endif
  result.statusMessage = statusMessage_;
  return result;
}

void AnimationStateMachineWorkspaceController::DrawParametersWindow() {
#if defined(HIKARI_WITH_EDITOR)
  if (!ImGui::Begin("Parameters###AnimationSM/Parameters")) {
    ImGui::End();
    return;
  }
  if (IconTextButton(EditorGlyph::Add, "Parameter", "AnimationSmAddParameter",
                     EditorButtonTone::Neutral, ImVec2(0.0f, 28.0f),
                     "Add a state machine parameter")) {
    ImGui::OpenPopup("AddAnimationParameter");
  }
  if (ImGui::BeginPopup("AddAnimationParameter")) {
    using Type = ANIMATION::AnimationParameterType;
    for (const auto [label, type] :
         {std::pair{"Bool", Type::Bool}, std::pair{"Float", Type::Float},
          std::pair{"Integer", Type::Integer},
          std::pair{"Trigger", Type::Trigger}}) {
      if (ImGui::MenuItem(label))
        AddParameter(type);
    }
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(selectionKind_ != SelectionKind::Parameter);
  if (IconButton(EditorGlyph::Delete, "AnimationSmDeleteParameter",
                 EditorButtonTone::Danger, ImVec2(28.0f, 28.0f),
                 "Delete selected parameter")) {
    DeleteSelectedParameter();
  }
  ImGui::EndDisabled();
  ImGui::Separator();
  for (const auto &parameter : document_.Definition().parameters) {
    const bool selected = selectionKind_ == SelectionKind::Parameter &&
                          selectedParameterId_ == parameter.id;
    const std::string label =
        parameter.name + "  [" + ParameterTypeName(parameter.type) +
        "]##Parameter" + std::to_string(parameter.id.value);
    if (ImGui::Selectable(label.c_str(), selected)) {
      selectionKind_ = SelectionKind::Parameter;
      selectedParameterId_ = parameter.id;
    }
  }
  if (document_.Definition().parameters.empty()) {
    ImGui::TextDisabled("Add parameters for gameplay facts or C++ controls.");
  }
  ImGui::End();
#endif
}

void AnimationStateMachineWorkspaceController::DrawTransitionsWindow() {
#if defined(HIKARI_WITH_EDITOR)
  if (!ImGui::Begin("Transitions###AnimationSM/Transitions")) {
    ImGui::End();
    return;
  }
  ImGui::BeginDisabled(document_.Definition().states.size() < 2u);
  if (IconTextButton(EditorGlyph::Add, "Transition", "AnimationSmAddTransition",
                     EditorButtonTone::Neutral, ImVec2(0.0f, 28.0f),
                     "Add a state transition")) {
    AddTransition();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(selectionKind_ != SelectionKind::Transition);
  if (IconButton(EditorGlyph::Delete, "AnimationSmDeleteTransition",
                 EditorButtonTone::Danger, ImVec2(28.0f, 28.0f),
                 "Delete selected transition")) {
    DeleteSelectedTransition();
  }
  ImGui::EndDisabled();
  ImGui::Separator();
  const auto &definition = document_.Definition();
  for (const auto &transition : definition.transitions) {
    const auto *source =
        ANIMATION::FindAnimationState(definition, transition.sourceStateId);
    const auto *target =
        ANIMATION::FindAnimationState(definition, transition.targetStateId);
    const std::string label =
        (source != nullptr ? source->name : "Any State") + "  ->  " +
        (target != nullptr ? target->name : "<missing>") + "##Transition" +
        std::to_string(transition.id.value);
    const bool selected = selectionKind_ == SelectionKind::Transition &&
                          selectedTransitionId_ == transition.id;
    if (ImGui::Selectable(label.c_str(), selected)) {
      selectionKind_ = SelectionKind::Transition;
      selectedTransitionId_ = transition.id;
    }
    if (ImGui::BeginPopupContextItem()) {
      if (ImGui::MenuItem("Delete Transition")) {
        selectedTransitionId_ = transition.id;
        selectionKind_ = SelectionKind::Transition;
        DeleteSelectedTransition();
        ImGui::EndPopup();
        break;
      }
      ImGui::EndPopup();
    }
  }
  ImGui::End();
#endif
}

void AnimationStateMachineWorkspaceController::DrawDiagnosticsWindow(
    DocumentSceneBase &scene) {
#if defined(HIKARI_WITH_EDITOR)
  if (!ImGui::Begin("Diagnostics###AnimationSM/Diagnostics")) {
    ImGui::End();
    return;
  }
  const auto issues =
      ANIMATION::ValidateAnimationStateMachine(document_.Definition());
  if (issues.empty()) {
    ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.55f, 1.0f),
                       "Ready: state graph is valid");
  } else {
    for (const auto &issue : issues) {
      ImGui::TextColored(
          issue.error ? ImVec4(1.0f, 0.4f, 0.35f, 1.0f)
                      : ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
          "%s%s", issue.error ? "Error: " : "Warning: ", issue.message.c_str());
    }
  }
  if (!statusMessage_.empty()) {
    ImGui::Separator();
    ImGui::TextWrapped("%s", statusMessage_.c_str());
  }
  DrawRuntimeDebug(scene);
  ImGui::End();
#else
  (void)scene;
#endif
}

} // namespace HIKARI::EDITOR
