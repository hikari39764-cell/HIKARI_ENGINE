#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {

MaterialGraphData *EditableGraph(MaterialAssetData &source) noexcept {
  if (source.kind == MaterialAssetKind::Material) {
    return &source.graph;
  }
  if (source.kind == MaterialAssetKind::Function) {
    return &source.function.graph;
  }
  return nullptr;
}

#if defined(HIKARI_WITH_EDITOR)
ImU32 NodeHeaderColor(MaterialNodeKind kind) noexcept {
  switch (kind) {
  case MaterialNodeKind::PbrSurface:
  case MaterialNodeKind::SurfaceOutput:
    return IM_COL32(92, 66, 142, 255);
  case MaterialNodeKind::RampDiffuse:
  case MaterialNodeKind::MultiRampDiffuse:
  case MaterialNodeKind::SpecularBand:
  case MaterialNodeKind::FresnelRim:
  case MaterialNodeKind::MatCap:
  case MaterialNodeKind::AnisotropicHair:
  case MaterialNodeKind::FaceSdf:
  case MaterialNodeKind::EyeLighting:
    return IM_COL32(40, 112, 126, 255);
  case MaterialNodeKind::TextureSample:
  case MaterialNodeKind::Parameter:
  case MaterialNodeKind::Constant:
    return IM_COL32(86, 94, 108, 255);
  default:
    return IM_COL32(83, 74, 56, 255);
  }
}

ImU32 PinColor(std::string_view pin) noexcept {
  if (pin == "Surface") return IM_COL32(186, 128, 255, 255);
  if (pin.find("Response") != std::string_view::npos) {
    return IM_COL32(255, 166, 76, 255);
  }
  if (pin == "Color" || pin == "RGB" || pin == "Additive" ||
      pin == "Emissive" || pin == "BaseColor" || pin == "SpecularColor") {
    return IM_COL32(109, 208, 232, 255);
  }
  return IM_COL32(188, 205, 214, 255);
}

std::string PinKey(std::string_view nodeId, std::string_view pin) {
  std::string key{};
  key.reserve(nodeId.size() + pin.size() + 1u);
  key.append(nodeId);
  key.push_back(':');
  key.append(pin);
  return key;
}
#endif

} // namespace

using MATERIAL_GRAPH_UI::FindNode;
using MATERIAL_GRAPH_UI::InputPins;
using MATERIAL_GRAPH_UI::OutputPins;

void ModelMaterialWorkspaceController::DrawGraphWindow(
    DocumentSceneBase &scene) {
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

  bool requestFrameAll = false;
  bool requestFocusSelection = false;
  bool requestDeleteNode = false;
  bool requestDeleteEdge = false;
  if (ImGui::Button("Add Node...")) {
    ImGui::OpenPopup("##MaterialGraphAddNode");
  }
  ImGui::SameLine();
  requestFrameAll = ImGui::Button("Frame All");
  ImGui::SameLine();
  ImGui::BeginDisabled(selectedGraphNodeId_.empty());
  requestFocusSelection = ImGui::Button("Focus Selection");
  ImGui::SameLine();
  requestDeleteNode = ImGui::Button("Delete Node");
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextDisabled("Drag output to input | MMB pan | wheel zoom | Del remove");

  if (ImGui::BeginPopup("##MaterialGraphAddNode")) {
    const MATH::Vec2 position{
        (360.0f - graphPanX_) / graphZoom_,
        (180.0f - graphPanY_) / graphZoom_};
    DrawMaterialGraphNodePalette(scene, source, *graph, position);
    ImGui::EndPopup();
  }

  requestDeleteEdge = DrawMaterialGraphInspector(scene, source, *graph);
  ImGui::SameLine();

  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImVec2 size = ImGui::GetContentRegionAvail();
  size.x = (std::max)(size.x, 320.0f);
  size.y = (std::max)(size.y, 240.0f);
  const ImVec2 canvasMax{origin.x + size.x, origin.y + size.y};
  ImDrawList *drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(origin, canvasMax, IM_COL32(15, 18, 23, 255));
  ImGui::SetCursorScreenPos(origin);
  ImGui::InvisibleButton("##MaterialGraphCanvas", size,
                         ImGuiButtonFlags_MouseButtonMiddle |
                             ImGuiButtonFlags_MouseButtonRight);
  const bool canvasHovered = ImGui::IsItemHovered();
  ImGuiIO &io = ImGui::GetIO();
  if (canvasHovered && io.MouseWheel != 0.0f) {
    const float oldZoom = graphZoom_;
    const float newZoom = std::clamp(
        graphZoom_ * std::pow(1.12f, io.MouseWheel), 0.35f, 2.5f);
    const ImVec2 mouse = io.MousePos;
    const float worldX = (mouse.x - origin.x - graphPanX_) / oldZoom;
    const float worldY = (mouse.y - origin.y - graphPanY_) / oldZoom;
    graphZoom_ = newZoom;
    graphPanX_ = mouse.x - origin.x - worldX * graphZoom_;
    graphPanY_ = mouse.y - origin.y - worldY * graphZoom_;
  }
  if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
    graphPanX_ += io.MouseDelta.x;
    graphPanY_ += io.MouseDelta.y;
  }

  constexpr float kNodeWidth = 220.0f;
  constexpr float kHeaderHeight = 34.0f;
  constexpr float kPinRowHeight = 22.0f;
  constexpr float kNodePadding = 9.0f;
  struct NodeLayout {
    ImVec2 position{};
    ImVec2 size{};
    std::vector<std::string_view> inputs{};
    std::vector<std::string_view> outputs{};
  };
  std::unordered_map<std::string, NodeLayout> layouts{};
  std::unordered_map<std::string, ImVec2> inputPositions{};
  std::unordered_map<std::string, ImVec2> outputPositions{};
  for (const MaterialGraphNodeData &node : graph->nodes) {
    NodeLayout layout{};
    layout.inputs = InputPins(node.kind);
    layout.outputs = OutputPins(node.kind);
    const size_t rows = (std::max)(
        size_t{1u}, (std::max)(layout.inputs.size(), layout.outputs.size()));
    layout.position = {
        origin.x + graphPanX_ + node.position.x * graphZoom_,
        origin.y + graphPanY_ + node.position.y * graphZoom_};
    layout.size = {
        kNodeWidth * graphZoom_,
        (kHeaderHeight + kNodePadding + rows * kPinRowHeight) * graphZoom_};
    for (size_t index = 0; index < layout.inputs.size(); ++index) {
      inputPositions[PinKey(node.id, layout.inputs[index])] = {
          layout.position.x,
          layout.position.y +
              (kHeaderHeight + kNodePadding +
               (static_cast<float>(index) + 0.5f) * kPinRowHeight) *
                  graphZoom_};
    }
    for (size_t index = 0; index < layout.outputs.size(); ++index) {
      outputPositions[PinKey(node.id, layout.outputs[index])] = {
          layout.position.x + layout.size.x,
          layout.position.y +
              (kHeaderHeight + kNodePadding +
               (static_cast<float>(index) + 0.5f) * kPinRowHeight) *
                  graphZoom_};
    }
    layouts.emplace(node.id, std::move(layout));
  }

  if (requestFrameAll && !graph->nodes.empty()) {
    float minX = graph->nodes.front().position.x;
    float minY = graph->nodes.front().position.y;
    float maxX = minX + kNodeWidth;
    float maxY = minY + 120.0f;
    for (const MaterialGraphNodeData &node : graph->nodes) {
      const auto layout = layouts.find(node.id);
      const float nodeWidth = kNodeWidth;
      const float nodeHeight = layout != layouts.end()
                                   ? layout->second.size.y / graphZoom_
                                   : 120.0f;
      minX = (std::min)(minX, node.position.x);
      minY = (std::min)(minY, node.position.y);
      maxX = (std::max)(maxX, node.position.x + nodeWidth);
      maxY = (std::max)(maxY, node.position.y + nodeHeight);
    }
    const float width = (std::max)(maxX - minX, 1.0f);
    const float height = (std::max)(maxY - minY, 1.0f);
    graphZoom_ = std::clamp(
        (std::min)((size.x - 80.0f) / width, (size.y - 80.0f) / height),
        0.35f, 2.5f);
    graphPanX_ = (size.x - width * graphZoom_) * 0.5f - minX * graphZoom_;
    graphPanY_ = (size.y - height * graphZoom_) * 0.5f - minY * graphZoom_;
  } else if (requestFocusSelection) {
    if (const MaterialGraphNodeData *node =
            FindNode(*graph, selectedGraphNodeId_)) {
      graphPanX_ = size.x * 0.5f -
                   (node->position.x + kNodeWidth * 0.5f) * graphZoom_;
      graphPanY_ = size.y * 0.5f -
                   (node->position.y + 60.0f) * graphZoom_;
    }
  }

  drawList->PushClipRect(origin, canvasMax, true);
  const float gridStep = 32.0f * graphZoom_;
  if (gridStep >= 10.0f) {
    for (float x = std::fmod(graphPanX_, gridStep); x < size.x;
         x += gridStep) {
      drawList->AddLine({origin.x + x, origin.y},
                        {origin.x + x, canvasMax.y},
                        IM_COL32(35, 40, 48, 160));
    }
    for (float y = std::fmod(graphPanY_, gridStep); y < size.y;
         y += gridStep) {
      drawList->AddLine({origin.x, origin.y + y},
                        {canvasMax.x, origin.y + y},
                        IM_COL32(35, 40, 48, 160));
    }
  }

  for (const MaterialGraphEdgeData &edge : graph->edges) {
    const auto start = outputPositions.find(PinKey(edge.sourceNodeId,
                                                   edge.sourcePin));
    const auto end = inputPositions.find(PinKey(edge.targetNodeId,
                                                edge.targetPin));
    if (start == outputPositions.end() || end == inputPositions.end()) {
      continue;
    }
    const float tangent = (std::max)(60.0f * graphZoom_,
                                     std::abs(end->second.x - start->second.x) *
                                         0.45f);
    drawList->AddBezierCubic(
        start->second, {start->second.x + tangent, start->second.y},
        {end->second.x - tangent, end->second.y}, end->second,
        selectedGraphEdgeId_ == edge.id ? IM_COL32(255, 190, 78, 255)
                                        : PinColor(edge.sourcePin),
        selectedGraphEdgeId_ == edge.id ? 3.5f : 2.25f);
  }

  struct CompletedLink {
    std::string targetNode{};
    std::string targetPin{};
  };
  std::optional<CompletedLink> completedLink{};
  bool pointerOverNode = false;
  bool nodeGestureEnded = false;
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
        {nodeMax.x, layout.position.y + kHeaderHeight * graphZoom_},
        NodeHeaderColor(node.kind), 7.0f,
        ImDrawFlags_RoundCornersTop);
    drawList->AddRect(layout.position, nodeMax,
                      selected ? IM_COL32(116, 226, 235, 255)
                               : IM_COL32(69, 82, 96, 255),
                      7.0f, 0, selected ? 2.5f : 1.5f);
    drawList->AddText(
        {layout.position.x + 10.0f * graphZoom_,
         layout.position.y + 8.0f * graphZoom_},
        IM_COL32(244, 247, 250, 255),
        node.displayName.empty() ? ToString(node.kind).data()
                                 : node.displayName.c_str());

    ImGui::PushID(node.id.c_str());
    ImGui::SetCursorScreenPos(layout.position);
    ImGui::InvisibleButton(
        "Header",
        {layout.size.x, kHeaderHeight * graphZoom_},
        ImGuiButtonFlags_MouseButtonLeft);
    pointerOverNode = pointerOverNode || ImGui::IsItemHovered();
    if (ImGui::IsItemClicked()) {
      selectedGraphNodeId_ = node.id;
      selectedGraphEdgeId_.clear();
    }
    if (ImGui::IsItemActivated() && !materialGesture_) {
      materialGesture_ = MaterialEditGesture{source, "Move material graph node"};
    }
    if (ImGui::IsItemActive() &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      node.position.x += io.MouseDelta.x / graphZoom_;
      node.position.y += io.MouseDelta.y / graphZoom_;
    }
    nodeGestureEnded = nodeGestureEnded ||
                       (ImGui::IsItemDeactivated() && materialGesture_.has_value());

    for (size_t index = 0; index < layout.inputs.size(); ++index) {
      const std::string_view pin = layout.inputs[index];
      const ImVec2 port = inputPositions.at(PinKey(node.id, pin));
      drawList->AddCircleFilled(port, 5.5f * graphZoom_, PinColor(pin));
      drawList->AddText({port.x + 10.0f * graphZoom_,
                         port.y - 7.0f * graphZoom_},
                        IM_COL32(204, 213, 222, 255), pin.data());
      ImGui::PushID(static_cast<int>(index));
      ImGui::SetCursorScreenPos(
          {port.x - 9.0f * graphZoom_, port.y - 9.0f * graphZoom_});
      ImGui::InvisibleButton("InputPin",
                             {18.0f * graphZoom_, 18.0f * graphZoom_});
      const bool hovered = ImGui::IsItemHovered(
          ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
      pointerOverNode = pointerOverNode || hovered;
      if (graphLinkDragActive_ && hovered &&
          ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        completedLink = CompletedLink{node.id, std::string(pin)};
      }
      ImGui::PopID();
    }
    for (size_t index = 0; index < layout.outputs.size(); ++index) {
      const std::string_view pin = layout.outputs[index];
      const ImVec2 port = outputPositions.at(PinKey(node.id, pin));
      drawList->AddCircleFilled(port, 5.5f * graphZoom_, PinColor(pin));
      const ImVec2 labelSize = ImGui::CalcTextSize(pin.data());
      drawList->AddText({port.x - labelSize.x - 10.0f * graphZoom_,
                         port.y - 7.0f * graphZoom_},
                        IM_COL32(204, 213, 222, 255), pin.data());
      ImGui::PushID(static_cast<int>(layout.inputs.size() + index));
      ImGui::SetCursorScreenPos(
          {port.x - 9.0f * graphZoom_, port.y - 9.0f * graphZoom_});
      ImGui::InvisibleButton("OutputPin",
                             {18.0f * graphZoom_, 18.0f * graphZoom_});
      const bool hovered = ImGui::IsItemHovered(
          ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
      pointerOverNode = pointerOverNode || hovered;
      if (ImGui::IsItemActivated()) {
        graphLinkDragActive_ = true;
        edgeSourceNodeId_ = node.id;
        edgeSourcePin_ = pin;
      }
      ImGui::PopID();
    }
    ImGui::PopID();
  }

  if (graphLinkDragActive_) {
    const auto start = outputPositions.find(
        PinKey(edgeSourceNodeId_, edgeSourcePin_));
    if (start != outputPositions.end()) {
      const ImVec2 mouse = io.MousePos;
      const float tangent = (std::max)(60.0f * graphZoom_,
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

  if (nodeGestureEnded) {
    CommitMaterialGesture(scene);
  }
  if (completedLink && edgeSourceNodeId_ != completedLink->targetNode) {
    MaterialAssetData before = source;
    std::erase_if(graph->edges, [&](const MaterialGraphEdgeData &edge) {
      return edge.targetNodeId == completedLink->targetNode &&
             edge.targetPin == completedLink->targetPin;
    });
    MaterialGraphEdgeData edge{};
    edge.id = "Edge_" + std::to_string(authoringIdCounter_++);
    edge.sourceNodeId = edgeSourceNodeId_;
    edge.sourcePin = edgeSourcePin_;
    edge.targetNodeId = completedLink->targetNode;
    edge.targetPin = completedLink->targetPin;
    selectedGraphEdgeId_ = edge.id;
    graph->edges.push_back(std::move(edge));
    CommitMaterialMutation(std::move(before), "Connect material graph nodes",
                           scene);
  }
  if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    graphLinkDragActive_ = false;
  }

  static MATH::Vec2 contextNodePosition{};
  const bool mouseInsideCanvas =
      io.MousePos.x >= origin.x && io.MousePos.x <= canvasMax.x &&
      io.MousePos.y >= origin.y && io.MousePos.y <= canvasMax.y;
  if (mouseInsideCanvas && !pointerOverNode &&
      ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    contextNodePosition = {
        (io.MousePos.x - origin.x - graphPanX_) / graphZoom_,
        (io.MousePos.y - origin.y - graphPanY_) / graphZoom_};
    ImGui::OpenPopup("##MaterialGraphCanvasContext");
  }
  if (ImGui::BeginPopup("##MaterialGraphCanvasContext")) {
    ImGui::TextDisabled("Add node at cursor");
    DrawMaterialGraphNodePalette(scene, source, *graph, contextNodePosition);
    ImGui::EndPopup();
  }

  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
    requestDeleteNode = !selectedGraphNodeId_.empty();
    requestDeleteEdge = !selectedGraphEdgeId_.empty();
  }
  if (requestDeleteNode && !selectedGraphNodeId_.empty()) {
    MaterialAssetData before = source;
    const std::string deletedId = selectedGraphNodeId_;
    std::erase_if(graph->nodes, [&deletedId](const auto &node) {
      return node.id == deletedId;
    });
    std::erase_if(graph->edges, [&deletedId](const auto &edge) {
      return edge.sourceNodeId == deletedId || edge.targetNodeId == deletedId;
    });
    if (graph->outputNodeId == deletedId) {
      graph->outputNodeId.clear();
    }
    selectedGraphNodeId_.clear();
    graphLinkDragActive_ = false;
    CommitMaterialMutation(std::move(before), "Delete material graph node",
                           scene);
  } else if (requestDeleteEdge && !selectedGraphEdgeId_.empty()) {
    MaterialAssetData before = source;
    const std::string deletedId = selectedGraphEdgeId_;
    std::erase_if(graph->edges, [&deletedId](const auto &edge) {
      return edge.id == deletedId;
    });
    selectedGraphEdgeId_.clear();
    CommitMaterialMutation(std::move(before), "Delete material graph edge",
                           scene);
  }

  ImGui::SetCursorScreenPos({origin.x, origin.y + size.y});
  ImGui::Dummy(ImVec2(1.0f, 1.0f));
  ImGui::End();
#else
  (void)scene;
#endif
}

} // namespace HIKARI::EDITOR
