#include "Editor/Shell/HIKARI_EditorDockLayout.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_AsciiCase.h"
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
    return TEXT::LocalizeCStr("editor.ui.float.64435f");
  case ANIMATION::AnimationParameterType::Integer:
    return TEXT::LocalizeCStr("editor.ui.integer.9edcff");
  case ANIMATION::AnimationParameterType::Trigger:
    return TEXT::LocalizeCStr("editor.ui.trigger.d3f06a");
  case ANIMATION::AnimationParameterType::Bool:
  default:
    return TEXT::LocalizeCStr("editor.ui.bool.daf760");
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
  HIKARI::EDITOR::DrawWorkspaceDockSpace(dockspaceId, viewport);
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
  if (!ImGui::Begin(TEXT::LocalizeWindow("editor.ui.parameters.a975ee", "AnimationSM/Parameters").c_str())) {
    ImGui::End();
    return;
  }
  if (IconTextButton(EditorGlyph::Add,TEXT::Localize("editor.ui.parameter.f699f2").c_str(), "AnimationSmAddParameter",
                     EditorButtonTone::Neutral, ImVec2(0.0f, 28.0f),TEXT::Localize("editor.ui.add.a.state.machine.parameter.106f36").c_str())) {
    ImGui::OpenPopup("AddAnimationParameter");
  }
  if (ImGui::BeginPopup("AddAnimationParameter")) {
    using Type = ANIMATION::AnimationParameterType;
    for (const auto [label, type] :
         {std::pair{"Bool", Type::Bool}, std::pair{"Float", Type::Float},
          std::pair{"Integer", Type::Integer},
          std::pair{"Trigger", Type::Trigger}}) {
      (void)label;
      if (ImGui::MenuItem(ParameterTypeName(type)))
        AddParameter(type);
    }
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(selectionKind_ != SelectionKind::Parameter);
  if (IconButton(EditorGlyph::Delete, "AnimationSmDeleteParameter",
                 EditorButtonTone::Danger, ImVec2(28.0f, 28.0f),TEXT::Localize("editor.ui.delete.selected.parameter.55f05d").c_str())) {
    DeleteSelectedParameter();
  }
  ImGui::EndDisabled();
  ImGui::Separator();
  SearchField("parameterSearch", TEXT::LocalizeCStr("shell.search"), parameterSearch_, sizeof(parameterSearch_));
  const auto filter = TEXT::ToLowerAsciiCopy(parameterSearch_);
  for (const auto &parameter : document_.Definition().parameters) {
    if (!filter.empty() && TEXT::ToLowerAsciiCopy(parameter.name).find(filter) == std::string::npos) continue;
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
    ImGui::TextDisabled(TEXT::Localize("editor.ui.add.parameters.for.gameplay.facts.or.c.controls.12ccc1").c_str());
  }
  ImGui::End();
#endif
}

void AnimationStateMachineWorkspaceController::DrawTransitionsWindow() {
#if defined(HIKARI_WITH_EDITOR)
  if (!ImGui::Begin(TEXT::LocalizeWindow("editor.ui.transitions.ab3926", "AnimationSM/Transitions").c_str())) {
    ImGui::End();
    return;
  }
  ImGui::BeginDisabled(document_.Definition().states.size() < 2u);
  if (IconTextButton(EditorGlyph::Add,TEXT::Localize("editor.ui.transition.4ead49").c_str(), "AnimationSmAddTransition",
                     EditorButtonTone::Neutral, ImVec2(0.0f, 28.0f),TEXT::Localize("editor.ui.add.a.state.transition.ff9e34").c_str())) {
    AddTransition();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(selectionKind_ != SelectionKind::Transition);
  if (IconButton(EditorGlyph::Delete, "AnimationSmDeleteTransition",
                 EditorButtonTone::Danger, ImVec2(28.0f, 28.0f),TEXT::Localize("editor.ui.delete.selected.transition.03ba0c").c_str())) {
    DeleteSelectedTransition();
  }
  ImGui::EndDisabled();
  ImGui::Separator();
  SearchField("transitionSearch", TEXT::LocalizeCStr("shell.search"), transitionSearch_, sizeof(transitionSearch_));
  ImGui::Checkbox(TEXT::LocalizeWindow("editor.workflow.connectedTransitions", "connectedTransitions").c_str(), &connectedTransitionsOnly_);
  const auto filter = TEXT::ToLowerAsciiCopy(transitionSearch_);
  const auto &definition = document_.Definition();
  for (const auto &transition : definition.transitions) {
    if (connectedTransitionsOnly_ && selectionKind_ == SelectionKind::State &&
        transition.sourceStateId.IsValid() && transition.sourceStateId != selectedStateId_ &&
        transition.targetStateId != selectedStateId_) continue;
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
    if (!filter.empty() && TEXT::ToLowerAsciiCopy(label).find(filter) == std::string::npos) continue;
    if (ImGui::Selectable(label.c_str(), selected)) {
      selectionKind_ = SelectionKind::Transition;
      selectedTransitionId_ = transition.id;
    }
    if (ImGui::BeginPopupContextItem()) {
      if (ImGui::MenuItem(TEXT::LocalizeWindow("editor.ui.delete.transition.527451", "Delete Transition").c_str())) {
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
  if (!ImGui::Begin(TEXT::LocalizeWindow("shell.diagnostics", "AnimationSM/Diagnostics").c_str())) {
    ImGui::End();
    return;
  }
  const auto issues =
      ANIMATION::ValidateAnimationStateMachine(document_.Definition());
  if (issues.empty()) {
    ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.55f, 1.0f),TEXT::Localize("editor.ui.ready.state.graph.is.valid.8ca6d7").c_str());
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
