#include "Editor/Workspaces/AnimationStateMachine/HIKARI_AnimationStateMachineGraphSupport.h"
#include "Editor/Workspaces/AnimationStateMachine/HIKARI_AnimationStateMachineWorkspaceController.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

#include "Animation/StateMachine/HIKARI_AnimationStateMachineInstance.h"
#include "Animation/StateMachine/HIKARI_AnimationStateMotionEvaluator.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

using namespace ANIMATION_STATE_MACHINE_GRAPH;
namespace {
#if defined(HIKARI_WITH_EDITOR)
constexpr ImVec2 kNodeWorldSize{168.0f, 72.0f};
constexpr float kMinimumGraphZoom = 0.35f;
constexpr float kMaximumGraphZoom = 2.0f;

#endif
} // namespace

void AnimationStateMachineWorkspaceController::DrawGraphWindow(
    DocumentSceneBase &scene, AnimationStateMachineWorkspaceResult &result,
    EditorCommandRouter &commandRouter) {
#if defined(HIKARI_WITH_EDITOR)
  GraphMenuRequests requests{};
  constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar |
                                     ImGuiWindowFlags_NoScrollWithMouse |
                                     ImGuiWindowFlags_NoScrollbar;
  if (!ImGui::Begin("State Graph###AnimationSM/Graph", nullptr, flags)) {
    ImGui::End();
    return;
  }

  DrawGraphMenuBar(scene, result, requests, commandRouter);

  const bool graphFocused =
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  ImGuiIO &io = ImGui::GetIO();
  if (CanUseEditorShortcut(EditorShortcutScope::Editing, graphFocused)) {
    if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
      requests.frameSelection = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
      requests.frameAll = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_1, false)) {
      requests.resetView = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
      if (selectionKind_ == SelectionKind::State) {
        DeleteSelectedState();
      } else if (selectionKind_ == SelectionKind::Transition) {
        DeleteSelectedTransition();
      }
    }
  }

  const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
  ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  canvasSize.x = (std::max)(canvasSize.x, 120.0f);
  canvasSize.y = (std::max)(canvasSize.y, 120.0f);
  const ImVec2 canvasEnd{canvasOrigin.x + canvasSize.x,
                         canvasOrigin.y + canvasSize.y};
  ImDrawList *drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(canvasOrigin, canvasEnd, IM_COL32(15, 18, 23, 255));
  drawList->PushClipRect(canvasOrigin, canvasEnd, true);

  const ImVec2 mouse = io.MousePos;
  const bool canvasHovered =
      Contains(canvasOrigin, canvasEnd, mouse) &&
      ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
  if (canvasHovered && io.MouseWheel != 0.0f) {
    const float previousZoom = graphZoom_;
    const float nextZoom =
        std::clamp(previousZoom * std::pow(1.15f, io.MouseWheel),
                   kMinimumGraphZoom, kMaximumGraphZoom);
    if (nextZoom != previousZoom) {
      const float graphMouseX =
          (mouse.x - canvasOrigin.x - graphPanX_) / previousZoom;
      const float graphMouseY =
          (mouse.y - canvasOrigin.y - graphPanY_) / previousZoom;
      graphZoom_ = nextZoom;
      graphPanX_ = mouse.x - canvasOrigin.x - graphMouseX * graphZoom_;
      graphPanY_ = mouse.y - canvasOrigin.y - graphMouseY * graphZoom_;
    }
  }
  if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
    graphPanX_ += io.MouseDelta.x;
    graphPanY_ += io.MouseDelta.y;
  }

  const float grid = 32.0f * graphZoom_;
  const float offsetX = std::fmod(graphPanX_, grid);
  const float offsetY = std::fmod(graphPanY_, grid);
  for (float x = offsetX; x < canvasSize.x; x += grid) {
    drawList->AddLine({canvasOrigin.x + x, canvasOrigin.y},
                      {canvasOrigin.x + x, canvasEnd.y},
                      IM_COL32(45, 52, 62, 100));
  }
  for (float y = offsetY; y < canvasSize.y; y += grid) {
    drawList->AddLine({canvasOrigin.x, canvasOrigin.y + y},
                      {canvasEnd.x, canvasOrigin.y + y},
                      IM_COL32(45, 52, 62, 100));
  }

  const auto frameAll = [&]() {
    const auto &states = document_.Definition().states;
    if (states.empty())
      return;
    float minimumX = (std::numeric_limits<float>::max)();
    float minimumY = (std::numeric_limits<float>::max)();
    float maximumX = std::numeric_limits<float>::lowest();
    float maximumY = std::numeric_limits<float>::lowest();
    for (const auto &state : states) {
      minimumX = (std::min)(minimumX, state.editorX);
      minimumY = (std::min)(minimumY, state.editorY);
      maximumX = (std::max)(maximumX, state.editorX + kNodeWorldSize.x);
      maximumY = (std::max)(maximumY, state.editorY + kNodeWorldSize.y);
    }
    const float width = (std::max)(maximumX - minimumX, 1.0f);
    const float height = (std::max)(maximumY - minimumY, 1.0f);
    graphZoom_ = std::clamp((std::min)((canvasSize.x - 120.0f) / width,
                                       (canvasSize.y - 120.0f) / height),
                            kMinimumGraphZoom, kMaximumGraphZoom);
    graphPanX_ =
        canvasSize.x * 0.5f - (minimumX + maximumX) * 0.5f * graphZoom_;
    graphPanY_ =
        canvasSize.y * 0.5f - (minimumY + maximumY) * 0.5f * graphZoom_;
  };
  const auto frameSelection = [&]() {
    const auto *state =
        ANIMATION::FindAnimationState(document_.Definition(), selectedStateId_);
    if (state == nullptr)
      return;
    graphPanX_ = canvasSize.x * 0.5f -
                 (state->editorX + kNodeWorldSize.x * 0.5f) * graphZoom_;
    graphPanY_ = canvasSize.y * 0.5f -
                 (state->editorY + kNodeWorldSize.y * 0.5f) * graphZoom_;
  };
  if (requests.resetView) {
    graphZoom_ = 1.0f;
    graphPanX_ = 24.0f;
    graphPanY_ = 24.0f;
  }
  if (requests.frameAll)
    frameAll();
  if (requests.frameSelection)
    frameSelection();
  if (requests.addStateAtCenter) {
    AddStateAt((canvasSize.x * 0.5f - graphPanX_) / graphZoom_ -
                   kNodeWorldSize.x * 0.5f,
               (canvasSize.y * 0.5f - graphPanY_) / graphZoom_ -
                   kNodeWorldSize.y * 0.5f);
  }

  const ImVec2 nodeSize{kNodeWorldSize.x * graphZoom_,
                        kNodeWorldSize.y * graphZoom_};
  const auto nodePosition = [&](const ANIMATION::AnimationState &state) {
    return ImVec2{canvasOrigin.x + graphPanX_ + state.editorX * graphZoom_,
                  canvasOrigin.y + graphPanY_ + state.editorY * graphZoom_};
  };
  const auto nodeCenter = [&](const ANIMATION::AnimationState &state) {
    const ImVec2 position = nodePosition(state);
    return ImVec2{position.x + nodeSize.x * 0.5f,
                  position.y + nodeSize.y * 0.5f};
  };

  const auto transitionEndpoints =
      [&](const ANIMATION::AnimationStateTransition &transition,
          ImVec2 &outStart, ImVec2 &outEnd) {
        const auto &definition = document_.Definition();
        const auto *target =
            ANIMATION::FindAnimationState(definition, transition.targetStateId);
        if (target == nullptr)
          return false;
        outEnd = nodeCenter(*target);
        outStart = {canvasOrigin.x + graphPanX_ + 24.0f * graphZoom_,
                    canvasOrigin.y + graphPanY_ + 24.0f * graphZoom_};
        if (const auto *source = ANIMATION::FindAnimationState(
                definition, transition.sourceStateId)) {
          outStart = nodeCenter(*source);
        }
        return true;
      };
  const auto findTransitionAt = [&](const ImVec2 &point) {
    ANIMATION::AnimationTransitionId resultId{};
    float nearest = 10.0f;
    for (const auto &transition : document_.Definition().transitions) {
      ImVec2 start{};
      ImVec2 end{};
      if (!transitionEndpoints(transition, start, end))
        continue;
      const float distance = DistanceToSegment(point, start, end);
      if (distance < nearest) {
        nearest = distance;
        resultId = transition.id;
      }
    }
    return resultId;
  };

  const auto &definition = document_.Definition();
  for (const auto &transition : definition.transitions) {
    ImVec2 start{};
    ImVec2 end{};
    if (!transitionEndpoints(transition, start, end))
      continue;
    const bool selected = selectionKind_ == SelectionKind::Transition &&
                          selectedTransitionId_ == transition.id;
    const ImU32 color =
        selected ? IM_COL32(255, 187, 80, 255) : IM_COL32(105, 145, 170, 210);
    drawList->AddLine(start, end, color,
                      (selected ? 3.0f : 2.0f) * (std::max)(0.7f, graphZoom_));
    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.001f)
      continue;
    const float ux = dx / length;
    const float uy = dy / length;
    const ImVec2 tip{end.x - ux * nodeSize.x * 0.48f,
                     end.y - uy * nodeSize.y * 0.48f};
    const ImVec2 side{-uy * 6.0f * graphZoom_, ux * 6.0f * graphZoom_};
    const ImVec2 back{tip.x - ux * 11.0f * graphZoom_,
                      tip.y - uy * 11.0f * graphZoom_};
    drawList->AddTriangleFilled(tip, {back.x + side.x, back.y + side.y},
                                {back.x - side.x, back.y - side.y}, color);
  }

  bool nodeHovered = false;
  ANIMATION::AnimationStateId transitionDropSource{};
  ANIMATION::AnimationStateId transitionDropTarget{};
  const auto *runtimeSnapshot = ResolveRuntimeDebugSnapshot(scene);
  const ANIMATION::AnimationStateId liveStateId =
      runtimeSnapshot != nullptr && runtimeSnapshot->running
          ? runtimeSnapshot->currentStateId
          : ANIMATION::AnimationStateId{};
  auto &mutableDefinition = document_.Definition();
  for (ANIMATION::AnimationState &state : mutableDefinition.states) {
    const ImVec2 position = nodePosition(state);
    const ImVec2 maximum{position.x + nodeSize.x, position.y + nodeSize.y};
    const bool selected =
        selectionKind_ == SelectionKind::State && selectedStateId_ == state.id;
    const bool entry = mutableDefinition.entryStateId == state.id;
    const bool live = liveStateId == state.id;
    const ImU32 fill = live       ? IM_COL32(70, 64, 34, 255)
                       : selected ? IM_COL32(49, 94, 111, 255)
                                  : IM_COL32(31, 39, 49, 255);
    const ImU32 border = live       ? IM_COL32(255, 203, 82, 255)
                         : entry    ? IM_COL32(95, 225, 150, 255)
                         : selected ? IM_COL32(93, 210, 225, 255)
                                    : IM_COL32(80, 94, 110, 255);
    drawList->AddRectFilled(position, maximum, fill, 6.0f * graphZoom_);
    drawList->AddRect(position, maximum, border, 6.0f * graphZoom_, 0,
                      2.0f * (std::max)(0.7f, graphZoom_));
    const float fontSize = ImGui::GetFontSize() * graphZoom_;
    drawList->AddText(
        ImGui::GetFont(), fontSize,
        {position.x + 10.0f * graphZoom_, position.y + 10.0f * graphZoom_},
        IM_COL32(235, 240, 245, 255), state.name.c_str());
    if (graphZoom_ >= 0.5f) {
      const std::string motionLabel = MotionLabel(state.motion);
      drawList->AddText(
          ImGui::GetFont(), fontSize,
          {position.x + 10.0f * graphZoom_, position.y + 39.0f * graphZoom_},
          ANIMATION::IsAnimationStateMotionEmpty(state.motion)
              ? IM_COL32(230, 155, 80, 255)
              : IM_COL32(160, 180, 195, 255),
          motionLabel.c_str());
    }
    if (entry && graphZoom_ >= 0.65f) {
      drawList->AddText(
          ImGui::GetFont(), fontSize * 0.8f,
          {maximum.x - 48.0f * graphZoom_, position.y + 10.0f * graphZoom_},
          IM_COL32(95, 225, 150, 255), "ENTRY");
    }
    if (live && graphZoom_ >= 0.65f) {
      drawList->AddText(
          ImGui::GetFont(), fontSize * 0.8f,
          {position.x + 10.0f * graphZoom_, maximum.y - 17.0f * graphZoom_},
          IM_COL32(255, 213, 100, 255), "LIVE");
    }

    ImGui::PushID(static_cast<int>(state.id.value));
    const float pinRadius = 7.0f * (std::max)(0.75f, graphZoom_);
    const ImVec2 pinCenter{maximum.x, position.y + nodeSize.y * 0.5f};
    drawList->AddCircleFilled(pinCenter, pinRadius,
                              transitionDragSourceStateId_ == state.id
                                  ? IM_COL32(255, 203, 82, 255)
                                  : IM_COL32(100, 180, 215, 255));
    ImGui::SetCursorScreenPos(
        {pinCenter.x - pinRadius * 1.5f, pinCenter.y - pinRadius * 1.5f});
    ImGui::InvisibleButton("TransitionPin",
                           {pinRadius * 3.0f, pinRadius * 3.0f});
    nodeHovered = nodeHovered || ImGui::IsItemHovered();
    if (ImGui::IsItemActivated()) {
      transitionDragSourceStateId_ = state.id;
    }
    if (ImGui::IsItemActive() && transitionDragSourceStateId_ == state.id) {
      drawList->AddLine(pinCenter, io.MousePos, IM_COL32(255, 203, 82, 230),
                        2.0f * (std::max)(0.7f, graphZoom_));
    }
    if (ImGui::IsItemDeactivated() &&
        transitionDragSourceStateId_ == state.id) {
      for (const auto &candidate : mutableDefinition.states) {
        const ImVec2 candidatePosition = nodePosition(candidate);
        const ImVec2 candidateMaximum{candidatePosition.x + nodeSize.x,
                                      candidatePosition.y + nodeSize.y};
        if (candidate.id != state.id &&
            Contains(candidatePosition, candidateMaximum, io.MousePos)) {
          transitionDropSource = state.id;
          transitionDropTarget = candidate.id;
          break;
        }
      }
      transitionDragSourceStateId_ = {};
    }

    ImGui::SetCursorScreenPos(position);
    ImGui::InvisibleButton("StateNode", nodeSize);
    nodeHovered = nodeHovered || ImGui::IsItemHovered();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
      selectionKind_ = SelectionKind::State;
      selectedStateId_ = state.id;
      selectedTransitionId_ = {};
    }
    if (ImGui::IsItemActive() &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      if (!graphDragBefore_)
        graphDragBefore_ = mutableDefinition;
      state.editorX += io.MouseDelta.x / graphZoom_;
      state.editorY += io.MouseDelta.y / graphZoom_;
    }
    const bool dragEnded =
        ImGui::IsItemDeactivated() && graphDragBefore_.has_value();
    ImGui::PopID();
    if (dragEnded) {
      RecordMutation(std::move(*graphDragBefore_), 9000u + state.id.value);
      graphDragBefore_.reset();
      break;
    }
  }

  if (transitionDropSource.IsValid() && transitionDropTarget.IsValid()) {
    AddTransitionBetween(transitionDropSource, transitionDropTarget);
  }

  if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
      !nodeHovered) {
    selectedTransitionId_ = findTransitionAt(mouse);
    if (selectedTransitionId_.IsValid()) {
      selectionKind_ = SelectionKind::Transition;
      selectedStateId_ = {};
    } else {
      selectionKind_ = SelectionKind::None;
      selectedStateId_ = {};
      selectedTransitionId_ = {};
    }
  }

  if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    graphContextStateId_ = {};
    graphContextTransitionId_ = {};
    for (const auto &state : document_.Definition().states) {
      const ImVec2 position = nodePosition(state);
      const ImVec2 maximum{position.x + nodeSize.x, position.y + nodeSize.y};
      if (Contains(position, maximum, mouse)) {
        graphContextStateId_ = state.id;
        break;
      }
    }
    if (graphContextStateId_.IsValid()) {
      selectionKind_ = SelectionKind::State;
      selectedStateId_ = graphContextStateId_;
      selectedTransitionId_ = {};
    } else {
      graphContextTransitionId_ = findTransitionAt(mouse);
      if (graphContextTransitionId_.IsValid()) {
        selectionKind_ = SelectionKind::Transition;
        selectedTransitionId_ = graphContextTransitionId_;
        selectedStateId_ = {};
      } else {
        selectionKind_ = SelectionKind::None;
        selectedStateId_ = {};
        selectedTransitionId_ = {};
      }
    }
    graphContextX_ = (mouse.x - canvasOrigin.x - graphPanX_) / graphZoom_ -
                     kNodeWorldSize.x * 0.5f;
    graphContextY_ = (mouse.y - canvasOrigin.y - graphPanY_) / graphZoom_ -
                     kNodeWorldSize.y * 0.5f;
    ImGui::OpenPopup("AnimationStateGraphContext");
  }

  drawList->PopClipRect();
  if (ImGui::BeginPopup("AnimationStateGraphContext")) {
    if (graphContextStateId_.IsValid()) {
      const auto *contextState = ANIMATION::FindAnimationState(
          document_.Definition(), graphContextStateId_);
      if (contextState != nullptr) {
        ImGui::TextDisabled("State: %s", contextState->name.c_str());
        ImGui::Separator();
        ImGui::BeginDisabled(document_.Definition().states.size() < 2u);
        if (ImGui::MenuItem("Create Transition")) {
          selectedStateId_ = graphContextStateId_;
          selectionKind_ = SelectionKind::State;
          AddTransition();
        }
        ImGui::EndDisabled();
        const bool isEntry =
            document_.Definition().entryStateId == graphContextStateId_;
        ImGui::BeginDisabled(isEntry);
        if (ImGui::MenuItem("Set as Entry State")) {
          auto before = document_.Definition();
          document_.Definition().entryStateId = graphContextStateId_;
          RecordMutation(std::move(before));
        }
        ImGui::EndDisabled();
        if (ImGui::MenuItem("Frame State")) {
          selectedStateId_ = graphContextStateId_;
          frameSelection();
        }
        ImGui::Separator();
        ImGui::BeginDisabled(document_.Definition().states.size() <= 1u);
        if (ImGui::MenuItem("Delete State")) {
          selectedStateId_ = graphContextStateId_;
          selectionKind_ = SelectionKind::State;
          DeleteSelectedState();
        }
        ImGui::EndDisabled();
      }
    } else if (graphContextTransitionId_.IsValid()) {
      ImGui::TextDisabled("Transition");
      ImGui::Separator();
      if (ImGui::MenuItem("Edit Transition")) {
        selectedTransitionId_ = graphContextTransitionId_;
        selectionKind_ = SelectionKind::Transition;
      }
      if (ImGui::MenuItem("Delete Transition")) {
        selectedTransitionId_ = graphContextTransitionId_;
        selectionKind_ = SelectionKind::Transition;
        DeleteSelectedTransition();
      }
    } else {
      if (ImGui::MenuItem("Add State Here")) {
        AddStateAt(graphContextX_, graphContextY_);
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Frame All States"))
        frameAll();
      if (ImGui::MenuItem("Reset View")) {
        graphZoom_ = 1.0f;
        graphPanX_ = 24.0f;
        graphPanY_ = 24.0f;
      }
    }
    ImGui::EndPopup();
  }

  const std::string zoomLabel =
      std::to_string(static_cast<int>(graphZoom_ * 100.0f)) + "%";
  const ImVec2 zoomTextSize = ImGui::CalcTextSize(zoomLabel.c_str());
  const ImVec2 zoomMinimum{canvasEnd.x - zoomTextSize.x - 24.0f,
                           canvasEnd.y - zoomTextSize.y - 16.0f};
  drawList->AddRectFilled(zoomMinimum, canvasEnd, IM_COL32(22, 27, 34, 220),
                          4.0f);
  drawList->AddText({zoomMinimum.x + 8.0f, zoomMinimum.y + 5.0f},
                    IM_COL32(160, 180, 195, 255), zoomLabel.c_str());
  drawList->AddText({canvasOrigin.x + 10.0f, canvasEnd.y - 22.0f},
                    IM_COL32(115, 132, 148, 220),
                    "RMB menu  |  Wheel zoom  |  MMB pan  |  F frame");

  ImGui::SetCursorScreenPos(canvasOrigin);
  ImGui::Dummy(canvasSize);
  ImGui::End();
#else
  (void)scene;
  (void)result;
#endif
}

} // namespace HIKARI::EDITOR
