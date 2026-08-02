#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif
namespace HIKARI::EDITOR {
using namespace MATERIAL_GRAPH_UI;
void ModelMaterialWorkspaceController::DrawGraphWindow(DocumentSceneBase &scene) {
#if defined(HIKARI_WITH_EDITOR)
  constexpr ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  if (!ImGui::Begin("Material Graph###ModelMaterial/Graph", nullptr, flags)) {
    ImGui::End();
    return;
  }
  if (!authoringDocument_.HasMaterial()) {
    ImGui::TextWrapped("Open or create a material to edit its graph.");
    ImGui::End();
    return;
  }
  MaterialAssetData &source = authoringDocument_.EditMaterial();
  MaterialGraphData *graph = EditableGraph(source);
  if (graph == nullptr) {
    ImGui::TextWrapped("This asset kind has no editable node graph.");
    ImGui::End();
    return;
  }
  std::string graphDocumentKey = authoringDocument_.GetMaterialAssetGuid().value + "|" +
      authoringDocument_.GetMaterialSourcePath().generic_string() + "|" + source.materialName;
  const bool graphDocumentChanged = graphCanvasDocumentKey_ != graphDocumentKey;
  if (graphDocumentChanged) {
    graphCanvasDocumentKey_ = std::move(graphDocumentKey);
    graphCanvas_ = {};
    collapsedGraphNodeIds_.clear();
  }
  bool requestFrameAll = graphDocumentChanged;
  bool requestFocusSelection = false, requestDeleteNode = false, requestDeleteEdge = false;
  if (DrawMaterialGraphToolbar(
          scene, source, *graph, requestFrameAll, requestFocusSelection,
          requestDeleteNode, requestDeleteEdge)) {
    ImGui::End();
    return;
  }
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImVec2 size = ImGui::GetContentRegionAvail();
  size.x = (std::max)(size.x, 320.0f);
  size.y = (std::max)(size.y, 240.0f);
  const ImVec2 canvasMax{origin.x + size.x, origin.y + size.y};
  ImDrawList *drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(origin, canvasMax, IM_COL32(15, 18, 23, 255));
  ImGui::SetCursorScreenPos(origin);
  ImGui::Dummy(size);
  ImGuiIO &io = ImGui::GetIO();
  graphCanvas_.zoom = std::clamp(
      graphCanvas_.zoom, kCanvasMinimumZoom, kCanvasMaximumZoom);
  const bool mouseInsideCanvas = io.MousePos.x >= origin.x &&
      io.MousePos.x <= canvasMax.x &&
      io.MousePos.y >= origin.y && io.MousePos.y <= canvasMax.y;
  const bool canvasHovered = mouseInsideCanvas && ImGui::IsWindowHovered();
  if (canvasHovered && io.MouseWheel != 0.0f) {
    NodeGraphCanvas::ZoomAt(graphCanvas_, {io.MousePos.x, io.MousePos.y},
        {origin.x, origin.y},
        io.MouseWheel, kCanvasMinimumZoom, kCanvasMaximumZoom);
  }
  if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
    NodeGraphCanvas::Pan(graphCanvas_, {io.MouseDelta.x, io.MouseDelta.y});
  }
  if (requestFrameAll && !graph->nodes.empty()) {
    float minX = graph->nodes.front().position.x;
    float minY = graph->nodes.front().position.y;
    float maxX = minX + kNodeWidth;
    float maxY = minY + 120.0f;
    for (const MaterialGraphNodeData &node : graph->nodes) {
      const float nodeHeight = NodeLogicalHeight(
          *graph, node, collapsedGraphNodeIds_.contains(node.id));
      minX = (std::min)(minX, node.position.x);
      minY = (std::min)(minY, node.position.y);
      maxX = (std::max)(maxX, node.position.x + kNodeWidth);
      maxY = (std::max)(maxY, node.position.y + nodeHeight);
    }
    NodeGraphCanvas::FrameBounds(graphCanvas_, {size.x, size.y},
        {minX, minY}, {maxX, maxY},
        80.0f, kCanvasMinimumZoom, kCanvasMaximumZoom);
  } else if (requestFocusSelection) {
    if (const MaterialGraphNodeData *node =
            FindNode(*graph, selectedGraphNodeId_)) {
      graphCanvas_.zoom = (std::max)(graphCanvas_.zoom, 1.0f);
      graphCanvas_.pan.x = size.x * 0.5f -
          (node->position.x + kNodeWidth * 0.5f) * graphCanvas_.zoom;
      graphCanvas_.pan.y = size.y * 0.5f -
          (node->position.y + NodeLogicalHeight(
              *graph, *node, collapsedGraphNodeIds_.contains(node->id)) *
                                  0.5f) * graphCanvas_.zoom;
    }
  }
  struct NodeLayout {
    ImVec2 position{};
    ImVec2 size{};
    std::vector<std::string_view> inputs{};
    std::vector<std::string_view> outputs{};
    float inlineHeight = 0.0f;
    bool collapsed = false;
  };
  std::unordered_map<std::string, NodeLayout> layouts{};
  std::unordered_map<std::string, ImVec2> inputPositions{}, outputPositions{};
  for (const MaterialGraphNodeData &node : graph->nodes) {
    NodeLayout layout{};
    layout.inputs = InputPins(node.kind);
    layout.outputs = OutputPins(node.kind);
    layout.collapsed = collapsedGraphNodeIds_.contains(node.id);
    if (layout.collapsed) {
      std::erase_if(layout.inputs, [&](std::string_view pin) {
        return !PinHasConnection(*graph, node.id, pin, false);
      });
      std::erase_if(layout.outputs, [&](std::string_view pin) {
        return !PinHasConnection(*graph, node.id, pin, true);
      });
    } else if (graphCanvas_.zoom >= kInlineControlsMinimumZoom) {
      layout.inlineHeight = NodeInlineHeight(node.kind);
    }
    const size_t rowInputCount = CountNodeInputRows(
        node.kind, layout.inputs, layout.inlineHeight > 0.0f);
    const size_t rows = (std::max)(size_t{1u},
        (std::max)(rowInputCount, layout.outputs.size()));
    const MATH::Vec2 screenPosition = NodeGraphCanvas::CanvasToScreen(
        graphCanvas_, {origin.x, origin.y}, node.position);
    layout.position = {screenPosition.x, screenPosition.y};
    layout.size = {
        kNodeWidth * graphCanvas_.zoom,
        (kHeaderHeight + kNodePadding + rows * kPinRowHeight) *
                graphCanvas_.zoom +
            layout.inlineHeight};
    const std::vector<float> inputOffsets =
        BuildNodeInputVerticalOffsets(
            node.kind, layout.inputs, layout.inlineHeight,
            graphCanvas_.zoom);
    for (size_t index = 0u; index < layout.inputs.size(); ++index) {
      inputPositions[PinKey(node.id, layout.inputs[index])] = {
          layout.position.x, layout.position.y + inputOffsets[index]};
    }
    for (size_t index = 0; index < layout.outputs.size(); ++index) {
      outputPositions[PinKey(node.id, layout.outputs[index])] = {
          layout.position.x + layout.size.x,
          layout.position.y +
              (kHeaderHeight + kNodePadding +
               (static_cast<float>(index) + 0.5f) * kPinRowHeight) *
                  graphCanvas_.zoom + layout.inlineHeight};
    }
    layouts.emplace(node.id, std::move(layout));
  }
  drawList->PushClipRect(origin, canvasMax, true);
  const float gridStep = 32.0f * graphCanvas_.zoom;
  if (gridStep >= 10.0f) {
    for (float x = std::fmod(graphCanvas_.pan.x, gridStep); x < size.x;
         x += gridStep) {
      drawList->AddLine({origin.x + x, origin.y},
                        {origin.x + x, canvasMax.y},
                        IM_COL32(35, 40, 48, 160));
    }
    for (float y = std::fmod(graphCanvas_.pan.y, gridStep); y < size.y;
         y += gridStep) {
      drawList->AddLine({origin.x, origin.y + y},
                        {canvasMax.x, origin.y + y},
                        IM_COL32(35, 40, 48, 160));
    }
  }
  const MATH::Vec2 pointer{io.MousePos.x, io.MousePos.y};
  std::string hoveredEdgeId{};
  for (const MaterialGraphEdgeData &edge : graph->edges) {
    const auto start = outputPositions.find(PinKey(edge.sourceNodeId,
                                                   edge.sourcePin));
    const auto end = inputPositions.find(PinKey(edge.targetNodeId,
                                                edge.targetPin));
    if (start == outputPositions.end() || end == inputPositions.end()) {
      continue;
    }
    const float tangent = (std::max)(60.0f * graphCanvas_.zoom,
                                     std::abs(end->second.x - start->second.x) *
                                         0.45f);
    const ImVec2 controlA{start->second.x + tangent, start->second.y};
    const ImVec2 controlB{end->second.x - tangent, end->second.y};
    if (mouseInsideCanvas && NodeGraphCanvas::HitBezierCubic(
            pointer,
            {start->second.x, start->second.y},
            {controlA.x, controlA.y},
            {controlB.x, controlB.y},
            {end->second.x, end->second.y},
            8.0f)) {
      hoveredEdgeId = edge.id;
    }
    const bool selected = selectedGraphEdgeId_ == edge.id;
    const bool hovered = hoveredEdgeId == edge.id;
    drawList->AddBezierCubic(
        start->second, controlA, controlB, end->second,
        selected ? IM_COL32(255, 190, 78, 255)
                 : hovered ? IM_COL32(225, 235, 242, 255)
                           : PinColor(edge.sourcePin),
        selected ? 3.5f : hovered ? 3.0f : 2.25f);
  }
  struct CompletedLink {
    std::string targetNode{};
    std::string targetPin{};
  };
  std::optional<CompletedLink> completedLink{};
  const float pinHitRadius = (std::max)(12.0f, 10.0f * graphCanvas_.zoom);
  struct PinHit {
    std::string nodeId{};
    std::string pin{};
  };
  const auto findPinHit = [&](bool output) -> std::optional<PinHit> {
    for (auto node = graph->nodes.rbegin(); node != graph->nodes.rend();
         ++node) {
      const auto layout = layouts.find(node->id);
      if (layout == layouts.end()) {
        continue;
      }
      const std::vector<std::string_view> &pins =
          output ? layout->second.outputs : layout->second.inputs;
      const auto &positions = output ? outputPositions : inputPositions;
      for (const std::string_view pin : pins) {
        const auto position = positions.find(PinKey(node->id, pin));
        if (position != positions.end() &&
            NodeGraphCanvas::HitPoint(
                pointer, {position->second.x, position->second.y},
                pinHitRadius)) {
          return PinHit{node->id, std::string(pin)};
        }
      }
    }
    return std::nullopt;
  };
  const auto findNodeHit = [&]() -> std::string {
    for (auto node = graph->nodes.rbegin(); node != graph->nodes.rend();
         ++node) {
      const auto layout = layouts.find(node->id);
      if (layout == layouts.end()) {
        continue;
      }
      const ImVec2 maximum{
          layout->second.position.x + layout->second.size.x,
          layout->second.position.y + layout->second.size.y};
      if (NodeGraphCanvas::HitRect(
              pointer,
              {layout->second.position.x, layout->second.position.y},
              {maximum.x, maximum.y})) {
        return node->id;
      }
    }
    return {};
  };
  const std::optional<PinHit> hoveredOutput = findPinHit(true);
  const std::optional<PinHit> hoveredInput = findPinHit(false);
  const std::string hoveredNodeId = findNodeHit();
  const auto hoveredLayout = layouts.find(hoveredNodeId);
  const bool pointerOverNodeControl = hoveredLayout != layouts.end() &&
      MATERIAL_GRAPH_UI::HitNodeAuthoringControls(
          pointer, {hoveredLayout->second.position.x,
                    hoveredLayout->second.position.y},
          {hoveredLayout->second.position.x + hoveredLayout->second.size.x,
           hoveredLayout->second.position.y + hoveredLayout->second.size.y},
          kHeaderHeight * graphCanvas_.zoom,
          hoveredLayout->second.inlineHeight);
  const bool pointerOverNodeHeader = hoveredLayout != layouts.end() &&
      pointer.y >= hoveredLayout->second.position.y &&
      pointer.y <= hoveredLayout->second.position.y +
          kHeaderHeight * graphCanvas_.zoom;
  const bool pointerOverNode = !hoveredNodeId.empty() ||
                               hoveredOutput.has_value() ||
                               hoveredInput.has_value();
  if (mouseInsideCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    if (hoveredOutput) {
      selectedGraphNodeId_ = hoveredOutput->nodeId;
      selectedGraphEdgeId_.clear();
      edgeSourceNodeId_ = hoveredOutput->nodeId;
      edgeSourcePin_ = hoveredOutput->pin;
      edgeTargetNodeId_.clear();
      edgeTargetPin_.clear();
      rewiredGraphEdgeId_.clear();
      graphLinkDragActive_ = true;
      draggedGraphNodeId_.clear();
    } else if (hoveredInput) {
      selectedGraphNodeId_ = hoveredInput->nodeId;
      draggedGraphNodeId_.clear();
      graphNodeDragStarted_ = false;
      const auto incoming = std::find_if(
          graph->edges.begin(), graph->edges.end(),
          [&](const MaterialGraphEdgeData &edge) {
            return edge.targetNodeId == hoveredInput->nodeId &&
                   edge.targetPin == hoveredInput->pin;
          });
      if (incoming != graph->edges.end()) {
        selectedGraphEdgeId_ = incoming->id;
        edgeSourceNodeId_ = incoming->sourceNodeId;
        edgeSourcePin_ = incoming->sourcePin;
        edgeTargetNodeId_ = incoming->targetNodeId;
        edgeTargetPin_ = incoming->targetPin;
        rewiredGraphEdgeId_ = incoming->id;
        graphLinkDragActive_ = true;
      } else {
        selectedGraphEdgeId_.clear();
        graphLinkDragActive_ = false;
        rewiredGraphEdgeId_.clear();
      }
    } else if (!hoveredNodeId.empty()) {
      selectedGraphNodeId_ = hoveredNodeId;
      selectedGraphEdgeId_.clear();
      draggedGraphNodeId_ = pointerOverNodeHeader && !pointerOverNodeControl
          ? hoveredNodeId : std::string{};
      graphNodeDragStarted_ = false;
      graphLinkDragActive_ = false;
      rewiredGraphEdgeId_.clear();
    } else if (!hoveredEdgeId.empty()) {
      selectedGraphNodeId_.clear();
      selectedGraphEdgeId_ = hoveredEdgeId;
      draggedGraphNodeId_.clear();
      graphNodeDragStarted_ = false;
      graphLinkDragActive_ = false;
      rewiredGraphEdgeId_.clear();
    } else {
      selectedGraphNodeId_.clear();
      selectedGraphEdgeId_.clear();
      draggedGraphNodeId_.clear();
      graphNodeDragStarted_ = false;
      graphLinkDragActive_ = false;
      rewiredGraphEdgeId_.clear();
    }
  }
  if (!draggedGraphNodeId_.empty() &&
      ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f)) {
    if (!graphNodeDragStarted_) {
      graphNodeDragStarted_ = true;
      if (!materialGesture_) {
        materialGesture_ =
            MaterialEditGesture{source, "Move material graph node"};
      }
    }
    if (MaterialGraphNodeData *dragged =
            FindNode(*graph, draggedGraphNodeId_)) {
      NodeGraphCanvas::MoveNode(
          graphCanvas_, dragged->position,
          {io.MouseDelta.x, io.MouseDelta.y});
    }
  }
  if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    if (graphLinkDragActive_ && hoveredInput) {
      completedLink =
          CompletedLink{hoveredInput->nodeId, hoveredInput->pin};
    }
    if (graphNodeDragStarted_ && materialGesture_) {
      CommitMaterialGesture(scene);
    }
    draggedGraphNodeId_.clear();
    graphNodeDragStarted_ = false;
  }
  bool inlineControlsCapture = false;
  for (MaterialGraphNodeData &node : graph->nodes) {
    const auto foundLayout = layouts.find(node.id);
    if (foundLayout == layouts.end()) {
      continue;
    }
    const NodeLayout &layout = foundLayout->second;
    const bool selected = selectedGraphNodeId_ == node.id;
    const ImVec2 nodeMax{layout.position.x + layout.size.x,
                         layout.position.y + layout.size.y};
    drawList->AddRectFilled(layout.position, nodeMax,
                            IM_COL32(29, 35, 43, 248), 7.0f);
    drawList->AddRectFilled(
        layout.position,
        {nodeMax.x, layout.position.y + kHeaderHeight * graphCanvas_.zoom},
        NodeHeaderColor(node.kind), 7.0f,
        ImDrawFlags_RoundCornersTop);
    if (selected) {
      drawList->AddRect(
          {layout.position.x - 2.0f, layout.position.y - 2.0f},
          {nodeMax.x + 2.0f, nodeMax.y + 2.0f},
          IM_COL32(255, 139, 56, 150), 9.0f, 0, 3.0f);
    }
    drawList->AddRect(layout.position, nodeMax,
                      selected ? IM_COL32(255, 171, 72, 255)
                               : IM_COL32(69, 82, 96, 255),
                      7.0f, 0, selected ? 3.0f : 1.5f);
    const float textScale = std::clamp(graphCanvas_.zoom, 0.72f, 1.15f);
    const float nodeFontSize = ImGui::GetFontSize() * textScale;
    drawList->AddText(
        ImGui::GetFont(), nodeFontSize,
        {layout.position.x + 10.0f * graphCanvas_.zoom,
         layout.position.y + 8.0f * graphCanvas_.zoom},
        IM_COL32(244, 247, 250, 255),
        node.displayName.empty() ? ToString(node.kind).data()
                                 : node.displayName.c_str(), nullptr);
    ImGui::PushID(node.id.c_str());
    ImGui::SetCursorScreenPos(
        {nodeMax.x - 25.0f, layout.position.y + 5.0f});
    if (ImGui::ArrowButton(
            "##CollapseNode",
            layout.collapsed ? ImGuiDir_Right : ImGuiDir_Down)) {
      if (layout.collapsed) {
        collapsedGraphNodeIds_.erase(node.id);
      } else {
        collapsedGraphNodeIds_.insert(node.id);
      }
    }
    inlineControlsCapture = inlineControlsCapture ||
                            ImGui::IsItemHovered() ||
                            ImGui::IsItemActive();
    ImGui::PopID();
    if (!layout.collapsed && layout.inlineHeight > 0.0f) {
      inlineControlsCapture =
          DrawMaterialGraphNodeInlineControls(
              scene, source, *graph, node,
              layout.position.x + 10.0f,
              layout.position.y +
                  kHeaderHeight * graphCanvas_.zoom + 5.0f,
              layout.size.x - 20.0f) ||
          inlineControlsCapture;
    }
    for (size_t index = 0; index < layout.inputs.size(); ++index) {
      const std::string_view pin = layout.inputs[index];
      const ImVec2 port = inputPositions.at(PinKey(node.id, pin));
      const bool hovered = hoveredInput && hoveredInput->nodeId == node.id &&
                           hoveredInput->pin == pin;
      drawList->AddCircleFilled(
          port, (hovered ? 7.0f : 5.5f) * graphCanvas_.zoom,
          PinColor(pin));
      const bool inlineOwned = layout.inlineHeight > 0.0f &&
          MATERIAL_GRAPH_UI::IsInlineOwnedInput(node.kind, pin);
      if (!inlineOwned && graphCanvas_.zoom >= 0.55f) {
        drawList->AddText(
            ImGui::GetFont(), nodeFontSize,
            {port.x + 10.0f * graphCanvas_.zoom,
             port.y - nodeFontSize * 0.5f},
            IM_COL32(204, 213, 222, 255), pin.data(), nullptr);
      }
    }
    for (size_t index = 0; index < layout.outputs.size(); ++index) {
      const std::string_view pin = layout.outputs[index];
      const ImVec2 port = outputPositions.at(PinKey(node.id, pin));
      const bool hovered = hoveredOutput &&
                           hoveredOutput->nodeId == node.id &&
                           hoveredOutput->pin == pin;
      drawList->AddCircleFilled(
          port, (hovered ? 7.0f : 5.5f) * graphCanvas_.zoom,
          PinColor(pin));
      if (graphCanvas_.zoom >= 0.55f) {
        const ImVec2 labelSize = ImGui::CalcTextSize(pin.data());
        drawList->AddText(
            ImGui::GetFont(), nodeFontSize,
            {port.x - labelSize.x * textScale - 10.0f * graphCanvas_.zoom,
             port.y - nodeFontSize * 0.5f},
            IM_COL32(204, 213, 222, 255), pin.data(), nullptr);
      }
    }
  }
  if (inlineControlsCapture && !graphNodeDragStarted_) {
    draggedGraphNodeId_.clear();
  }
  if (graphLinkDragActive_) {
    const auto start = outputPositions.find(
        PinKey(edgeSourceNodeId_, edgeSourcePin_));
    if (start != outputPositions.end()) {
      const ImVec2 mouse = io.MousePos;
      const float tangent = (std::max)(60.0f * graphCanvas_.zoom,
                                       std::abs(mouse.x - start->second.x) *
                                           0.45f);
      drawList->AddBezierCubic(
          start->second,
          {start->second.x + tangent, start->second.y},
          {mouse.x - tangent, mouse.y}, mouse,
          PinColor(edgeSourcePin_), 2.5f);
    }
  }
  drawList->PopClipRect();
  const bool linkReleased = graphLinkDragActive_ &&
      ImGui::IsMouseReleased(ImGuiMouseButton_Left);
  FinishMaterialGraphCanvasInteraction(
      scene, source, *graph,
      completedLink ? completedLink->targetNode : std::string{},
      completedLink ? completedLink->targetPin : std::string{},
      linkReleased, mouseInsideCanvas, pointerOverNode,
      hoveredNodeId, hoveredEdgeId,
      {origin.x, origin.y}, pointer,
      requestDeleteNode, requestDeleteEdge);
  DrawInlineRampEditorPopup(scene);
  ImGui::SetCursorScreenPos({origin.x, origin.y + size.y});
  ImGui::Dummy({1.0f, 1.0f});
  ImGui::End();
#else
  (void)scene;
#endif
}

} // namespace HIKARI::EDITOR
