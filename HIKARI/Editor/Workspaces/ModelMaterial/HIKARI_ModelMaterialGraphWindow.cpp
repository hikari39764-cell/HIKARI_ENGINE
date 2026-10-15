#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "imgui.h"
namespace HIKARI::EDITOR {
using namespace MATERIAL_GRAPH_UI;

void ModelMaterialWorkspaceController::DrawGraphWindow(DocumentSceneBase& scene) {
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!ImGui::Begin(TEXT::LocalizeWindow("editor.ui.material.graph.d78482", "ModelMaterial/Graph").c_str(), nullptr,
                      flags)) {
        ImGui::End();
        return;
    }
    if (!authoringDocument_.HasMaterial()) {
        ImGui::TextWrapped(TEXT::Localize("editor.ui.open.or.create.a.material.to.edit.its.graph.4a7980").c_str());
        ImGui::End();
        return;
    }
    MaterialAssetData& source = authoringDocument_.EditMaterial();
    MaterialGraphData* graph = EditableGraph(source);
    if (graph == nullptr) {
        ImGui::TextWrapped(TEXT::Localize("editor.ui.this.asset.kind.has.no.editable.node.graph.1593a8").c_str());
        ImGui::End();
        return;
    }
    std::string graphDocumentKey = authoringDocument_.GetMaterialAssetGuid().value + "|" +
                                   authoringDocument_.GetMaterialSourcePath().generic_string() + "|" +
                                   source.materialName;
    const bool graphDocumentChanged = graphCanvasDocumentKey_ != graphDocumentKey;
    if (graphDocumentChanged) {
        graphCanvasDocumentKey_ = std::move(graphDocumentKey);
        graphCanvas_ = {};
        collapsedGraphNodeIds_.clear();
    }
    bool requestFrameAll = graphDocumentChanged || graphFrameRequested_;
    graphFrameRequested_ = false;
    bool requestFocusSelection = graphFocusRequested_, requestDeleteNode = false, requestDeleteEdge = false;
    graphFocusRequested_ = false;
    if (DrawMaterialGraphToolbar(scene, source, *graph, requestFrameAll, requestFocusSelection, requestDeleteNode,
                                 requestDeleteEdge)) {
        ImGui::End();
        return;
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.x = (std::max)(size.x, 320.0f);
    size.y = (std::max)(size.y, 240.0f);
    const ImVec2 canvasMax{origin.x + size.x, origin.y + size.y};
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, canvasMax, IM_COL32(15, 18, 23, 255));
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(size);
    ImGuiIO& io = ImGui::GetIO();
    graphCanvas_.zoom = std::clamp(graphCanvas_.zoom, kCanvasMinimumZoom, kCanvasMaximumZoom);
    const bool mouseInsideCanvas = io.MousePos.x >= origin.x && io.MousePos.x <= canvasMax.x &&
                                   io.MousePos.y >= origin.y && io.MousePos.y <= canvasMax.y;
    const bool canvasHovered = mouseInsideCanvas && ImGui::IsWindowHovered();
    if (canvasHovered && io.MouseWheel != 0.0f) {
        NodeGraphCanvas::ZoomAt(graphCanvas_, {io.MousePos.x, io.MousePos.y}, {origin.x, origin.y}, io.MouseWheel,
                                kCanvasMinimumZoom, kCanvasMaximumZoom);
    }
    if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        NodeGraphCanvas::Pan(graphCanvas_, {io.MouseDelta.x, io.MouseDelta.y});
    }
    if (requestFrameAll) {
        FrameMaterialGraph(false, {size.x, size.y});
    } else if (requestFocusSelection) {
        FrameMaterialGraph(true, {size.x, size.y});
    }
    struct NodeLayout {
        ImVec2 position{};
        ImVec2 size{};
        NodeCardPresentation card{};
        float screenScale = 1.0f;
    };
    std::unordered_map<std::string, NodeLayout> layouts{};
    std::unordered_map<std::string, ImVec2> inputPositions{}, outputPositions{};
    for (const MaterialGraphNodeData& node : graph->nodes) {
        NodeLayout layout{};
        layout.card =
            ResolveNodeCardPresentation(*graph, node, graphCanvas_.zoom, collapsedGraphNodeIds_.contains(node.id));
        const bool authoringControls = layout.card.detail == NodeCardDetail::Authoring;
        // ImGui widgets are pixel-space controls. At the editable LOD the whole
        // card therefore uses one pixel-space scale instead of mixing zoomed
        // headers/pins with unscaled fields. Compact/overview cards contain no
        // widgets and can safely follow canvas zoom.
        layout.screenScale = authoringControls ? 1.0f : graphCanvas_.zoom;
        const float contentHeight = layout.card.inlineHeight + layout.card.compactSummaryHeight;
        const size_t rowInputCount = CountNodeInputRows(node.kind, layout.card.inputs, authoringControls);
        const size_t rows = (std::max)(size_t{1u}, (std::max)(rowInputCount, layout.card.outputs.size()));
        const MATH::Vec2 screenPosition =
            NodeGraphCanvas::CanvasToScreen(graphCanvas_, {origin.x, origin.y}, node.position);
        layout.position = {screenPosition.x, screenPosition.y};
        layout.size = {kNodeWidth * layout.screenScale,
                       (kHeaderHeight + kNodePadding + rows * kPinRowHeight) * layout.screenScale + contentHeight};
        const std::vector<float> inputOffsets = BuildNodeInputVerticalOffsets(
            node.kind, layout.card.inputs, contentHeight, authoringControls, layout.screenScale);
        for (size_t index = 0u; index < layout.card.inputs.size(); ++index) {
            inputPositions[PinKey(node.id, layout.card.inputs[index])] = {layout.position.x,
                                                                          layout.position.y + inputOffsets[index]};
        }
        for (size_t index = 0; index < layout.card.outputs.size(); ++index) {
            outputPositions[PinKey(node.id, layout.card.outputs[index])] = {
                layout.position.x + layout.size.x,
                layout.position.y +
                    (kHeaderHeight + kNodePadding + (static_cast<float>(index) + 0.5f) * kPinRowHeight) *
                        layout.screenScale +
                    contentHeight};
        }
        layouts.emplace(node.id, std::move(layout));
    }
    drawList->PushClipRect(origin, canvasMax, true);
    const float gridStep = 32.0f * graphCanvas_.zoom;
    if (gridStep >= 10.0f) {
        for (float x = std::fmod(graphCanvas_.pan.x, gridStep); x < size.x; x += gridStep) {
            drawList->AddLine({origin.x + x, origin.y}, {origin.x + x, canvasMax.y}, IM_COL32(35, 40, 48, 160));
        }
        for (float y = std::fmod(graphCanvas_.pan.y, gridStep); y < size.y; y += gridStep) {
            drawList->AddLine({origin.x, origin.y + y}, {canvasMax.x, origin.y + y}, IM_COL32(35, 40, 48, 160));
        }
    }
    std::unordered_map<std::string, ImVec4> groupScreenBounds;
    for (const auto& [id, layout] : layouts) {
        groupScreenBounds[id] = {layout.position.x, layout.position.y, layout.position.x + layout.size.x,
                                 layout.position.y + layout.size.y};
    }
    DrawMaterialGraphGroups(drawList, *graph, groupScreenBounds);
    const MATH::Vec2 pointer{io.MousePos.x, io.MousePos.y};
    std::string hoveredEdgeId{};
    for (const MaterialGraphEdgeData& edge : graph->edges) {
        const auto start = outputPositions.find(PinKey(edge.sourceNodeId, edge.sourcePin));
        const auto end = inputPositions.find(PinKey(edge.targetNodeId, edge.targetPin));
        if (start == outputPositions.end() || end == inputPositions.end()) {
            continue;
        }
        const float tangent = (std::max)(60.0f * graphCanvas_.zoom, std::abs(end->second.x - start->second.x) * 0.45f);
        const ImVec2 controlA{start->second.x + tangent, start->second.y};
        const ImVec2 controlB{end->second.x - tangent, end->second.y};
        if (mouseInsideCanvas &&
            NodeGraphCanvas::HitBezierCubic(pointer, {start->second.x, start->second.y}, {controlA.x, controlA.y},
                                            {controlB.x, controlB.y}, {end->second.x, end->second.y}, 8.0f)) {
            hoveredEdgeId = edge.id;
        }
        const bool selected = selectedGraphEdgeId_ == edge.id;
        const bool hovered = hoveredEdgeId == edge.id;
        drawList->AddBezierCubic(start->second, controlA, controlB, end->second,
                                 selected  ? IM_COL32(255, 190, 78, 255)
                                 : hovered ? IM_COL32(225, 235, 242, 255)
                                           : PinColor(edge.sourcePin),
                                 selected  ? 3.5f
                                 : hovered ? 3.0f
                                           : 2.25f);
    }
    struct CompletedLink {
        std::string targetNode{};
        std::string targetPin{};
    };
    std::optional<CompletedLink> completedLink{};
    struct PinHit {
        std::string nodeId{};
        std::string pin{};
    };
    const auto findPinHit = [&](bool output) -> std::optional<PinHit> {
        for (auto node = graph->nodes.rbegin(); node != graph->nodes.rend(); ++node) {
            const auto layout = layouts.find(node->id);
            if (layout == layouts.end()) {
                continue;
            }
            const std::vector<std::string_view>& pins =
                output ? layout->second.card.outputs : layout->second.card.inputs;
            const float pinHitRadius = (std::max)(12.0f, 10.0f * layout->second.screenScale);
            const auto& positions = output ? outputPositions : inputPositions;
            for (const std::string_view pin : pins) {
                const auto position = positions.find(PinKey(node->id, pin));
                if (position != positions.end() &&
                    NodeGraphCanvas::HitPoint(pointer, {position->second.x, position->second.y}, pinHitRadius)) {
                    return PinHit{node->id, std::string(pin)};
                }
            }
        }
        return std::nullopt;
    };
    const auto findNodeHit = [&]() -> std::string {
        for (auto node = graph->nodes.rbegin(); node != graph->nodes.rend(); ++node) {
            const auto layout = layouts.find(node->id);
            if (layout == layouts.end()) {
                continue;
            }
            const ImVec2 maximum{layout->second.position.x + layout->second.size.x,
                                 layout->second.position.y + layout->second.size.y};
            if (NodeGraphCanvas::HitRect(pointer, {layout->second.position.x, layout->second.position.y},
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
    const bool pointerOverNodeControl =
        hoveredLayout != layouts.end() &&
        MATERIAL_GRAPH_UI::HitNodeAuthoringControls(
            pointer, {hoveredLayout->second.position.x, hoveredLayout->second.position.y},
            {hoveredLayout->second.position.x + hoveredLayout->second.size.x,
             hoveredLayout->second.position.y + hoveredLayout->second.size.y},
            kHeaderHeight * hoveredLayout->second.screenScale, hoveredLayout->second.card.inlineHeight);
    const bool pointerOverNodeHeader =
        hoveredLayout != layouts.end() && pointer.y >= hoveredLayout->second.position.y &&
        pointer.y <= hoveredLayout->second.position.y + kHeaderHeight * hoveredLayout->second.screenScale;
    const bool pointerOverNode = !hoveredNodeId.empty() || hoveredOutput.has_value() || hoveredInput.has_value();
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
            const auto incoming =
                std::find_if(graph->edges.begin(), graph->edges.end(), [&](const MaterialGraphEdgeData& edge) {
                    return edge.targetNodeId == hoveredInput->nodeId && edge.targetPin == hoveredInput->pin;
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
            SetGraphNodeSelection(hoveredNodeId, io.KeyCtrl || io.KeyShift);
            selectedGraphEdgeId_.clear();
            draggedGraphNodeId_ = pointerOverNodeHeader && !pointerOverNodeControl ? hoveredNodeId : std::string{};
            graphNodeDragStarted_ = false;
            graphLinkDragActive_ = false;
            rewiredGraphEdgeId_.clear();
        } else if (!hoveredEdgeId.empty()) {
            ClearGraphNodeSelection();
            selectedGraphEdgeId_ = hoveredEdgeId;
            draggedGraphNodeId_.clear();
            graphNodeDragStarted_ = false;
            graphLinkDragActive_ = false;
            rewiredGraphEdgeId_.clear();
        } else {
            ClearGraphNodeSelection();
            selectedGraphEdgeId_.clear();
            draggedGraphNodeId_.clear();
            graphNodeDragStarted_ = false;
            graphLinkDragActive_ = false;
            rewiredGraphEdgeId_.clear();
        }
    }
    if (!draggedGraphNodeId_.empty() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f)) {
        if (!graphNodeDragStarted_) {
            graphNodeDragStarted_ = true;
            if (!authoringDocument_.HasMaterialGesture()) {
                authoringDocument_.BeginMaterialGesture(source, "Move material graph node");
            }
        }
        if (MaterialGraphNodeData* dragged = FindMaterialGraphNode(*graph, draggedGraphNodeId_)) {
            NodeGraphCanvas::MoveNode(graphCanvas_, dragged->position, {io.MouseDelta.x, io.MouseDelta.y});
        }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (graphLinkDragActive_ && hoveredInput) {
            completedLink = CompletedLink{hoveredInput->nodeId, hoveredInput->pin};
        }
        if (graphNodeDragStarted_ && authoringDocument_.HasMaterialGesture()) {
            authoringDocument_.EndMaterialGesture(false);
        }
        draggedGraphNodeId_.clear();
        graphNodeDragStarted_ = false;
    }
    bool inlineControlsCapture = false;
    for (MaterialGraphNodeData& node : graph->nodes) {
        const auto foundLayout = layouts.find(node.id);
        if (foundLayout == layouts.end()) {
            continue;
        }
        const NodeLayout& layout = foundLayout->second;
        const bool selected = selectedGraphNodeId_ == node.id || IsGraphNodeSelected(node.id);
        const ImVec2 nodeMax{layout.position.x + layout.size.x, layout.position.y + layout.size.y};
        drawList->AddRectFilled(layout.position, nodeMax, IM_COL32(29, 35, 43, 248), 7.0f);
        drawList->AddRectFilled(layout.position, {nodeMax.x, layout.position.y + kHeaderHeight * layout.screenScale},
                                NodeHeaderColor(node.kind), 7.0f, ImDrawFlags_RoundCornersTop);
        if (selected) {
            drawList->AddRect({layout.position.x - 2.0f, layout.position.y - 2.0f},
                              {nodeMax.x + 2.0f, nodeMax.y + 2.0f}, IM_COL32(255, 139, 56, 150), 9.0f, 0, 3.0f);
        }
        drawList->AddRect(layout.position, nodeMax, selected ? IM_COL32(255, 171, 72, 255) : IM_COL32(69, 82, 96, 255),
                          7.0f, 0, selected ? 3.0f : 1.5f);
        const float textScale = std::clamp(layout.screenScale, 0.72f, 1.15f);
        const float nodeFontSize = ImGui::GetFontSize() * textScale;
        drawList->AddText(
            ImGui::GetFont(), nodeFontSize,
            {layout.position.x + 10.0f * layout.screenScale, layout.position.y + 8.0f * layout.screenScale},
            IM_COL32(244, 247, 250, 255),
            node.displayName.empty() ? ToString(node.kind).data() : node.displayName.c_str(), nullptr);
        ImGui::PushID(node.id.c_str());
        const NodeCollapseControlResult collapse =
            DrawNodeCollapseControl({nodeMax.x, layout.position.y}, layout.screenScale, layout.card.collapsed);
        if (collapse.toggled) {
            if (layout.card.collapsed) {
                collapsedGraphNodeIds_.erase(node.id);
            } else {
                collapsedGraphNodeIds_.insert(node.id);
            }
        }
        inlineControlsCapture = inlineControlsCapture || collapse.capturesPointer;
        ImGui::PopID();
        if (!layout.card.collapsed && layout.card.inlineHeight > 0.0f) {
            inlineControlsCapture = DrawMaterialGraphNodeInlineControls(
                                        scene, source, *graph, node, layout.position.x + 10.0f * layout.screenScale,
                                        layout.position.y + kHeaderHeight * layout.screenScale + 5.0f,
                                        layout.size.x - 20.0f * layout.screenScale) ||
                                    inlineControlsCapture;
        } else if (layout.card.detail == NodeCardDetail::Compact) {
            drawList->AddText(ImGui::GetFont(), nodeFontSize,
                              {layout.position.x + 10.0f * layout.screenScale,
                               layout.position.y + kHeaderHeight * layout.screenScale + 6.0f * layout.screenScale},
                              IM_COL32(155, 176, 194, 255), "Compact view - Zoom in to edit node parameters", nullptr);
        }
        for (size_t index = 0; index < layout.card.inputs.size(); ++index) {
            const std::string_view pin = layout.card.inputs[index];
            const ImVec2 port = inputPositions.at(PinKey(node.id, pin));
            const bool hovered = hoveredInput && hoveredInput->nodeId == node.id && hoveredInput->pin == pin;
            drawList->AddCircleFilled(port, (hovered ? 7.0f : 5.5f) * layout.screenScale, PinColor(pin));
            const bool inlineOwned = layout.card.detail == NodeCardDetail::Authoring &&
                                     MATERIAL_GRAPH_UI::IsInlineOwnedInput(node.kind, pin);
            if (!inlineOwned && graphCanvas_.zoom >= 0.55f) {
                drawList->AddText(ImGui::GetFont(), nodeFontSize,
                                  {port.x + 10.0f * layout.screenScale, port.y - nodeFontSize * 0.5f},
                                  IM_COL32(204, 213, 222, 255), pin.data(), nullptr);
            }
        }
        for (size_t index = 0; index < layout.card.outputs.size(); ++index) {
            const std::string_view pin = layout.card.outputs[index];
            const ImVec2 port = outputPositions.at(PinKey(node.id, pin));
            const bool hovered = hoveredOutput && hoveredOutput->nodeId == node.id && hoveredOutput->pin == pin;
            drawList->AddCircleFilled(port, (hovered ? 7.0f : 5.5f) * layout.screenScale, PinColor(pin));
            if (graphCanvas_.zoom >= 0.55f) {
                const ImVec2 labelSize = ImGui::CalcTextSize(pin.data());
                drawList->AddText(
                    ImGui::GetFont(), nodeFontSize,
                    {port.x - labelSize.x * textScale - 10.0f * layout.screenScale, port.y - nodeFontSize * 0.5f},
                    IM_COL32(204, 213, 222, 255), pin.data(), nullptr);
            }
        }
    }
    if (inlineControlsCapture && !graphNodeDragStarted_) {
        draggedGraphNodeId_.clear();
    }
    if (graphLinkDragActive_) {
        const auto start = outputPositions.find(PinKey(edgeSourceNodeId_, edgeSourcePin_));
        if (start != outputPositions.end()) {
            DrawPendingLink({start->second.x, start->second.y}, {io.MousePos.x, io.MousePos.y}, graphCanvas_.zoom,
                            PinColor(edgeSourcePin_));
        }
    }
    drawList->PopClipRect();
    const bool linkReleased = graphLinkDragActive_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    FinishMaterialGraphCanvasInteraction(
        scene, source, *graph, completedLink ? completedLink->targetNode : std::string{},
        completedLink ? completedLink->targetPin : std::string{}, linkReleased, mouseInsideCanvas, pointerOverNode,
        hoveredNodeId, hoveredEdgeId, {origin.x, origin.y}, pointer, requestDeleteNode, requestDeleteEdge);
    DrawInlineRampEditorPopup(scene);
    ImGui::SetCursorScreenPos({origin.x, origin.y + size.y});
    ImGui::Dummy({1.0f, 1.0f});
    ImGui::End();
}
} // namespace HIKARI::EDITOR
