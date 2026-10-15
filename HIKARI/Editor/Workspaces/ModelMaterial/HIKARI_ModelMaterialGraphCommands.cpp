#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"

#include <algorithm>
#include <utility>

#include "Assets/Material/Graph/HIKARI_MaterialGraphValidation.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#include "imgui.h"

namespace HIKARI::EDITOR {

namespace {

void RunMaterialOperation(const EditorOperationRegistry* registry, const std::string& operationId,
                          std::string& outStatusMessage) {
    if (registry == nullptr) {
        outStatusMessage = "material operation bus is not ready";
        return;
    }
    const EditorOperationResult result = registry->Execute(operationId);
    outStatusMessage = result.message;
}

} // namespace

bool ModelMaterialWorkspaceController::DrawMaterialGraphToolbar(DocumentSceneBase& scene, MaterialAssetData& source,
                                                                MaterialGraphData& graph, bool& requestFrameAll,
                                                                bool& requestFocusSelection, bool& requestDeleteNode,
                                                                bool& requestDeleteEdge) {
    const bool hasSelection = !selectedGraphNodeIds_.empty() || !selectedGraphNodeId_.empty();
    std::string operation;
    bool paste = false, extract = false;
    const auto button = [&](EditorGlyph glyph, const char* id, const char* key, bool enabled = true) {
        NextToolbarItem();
        ImGui::BeginDisabled(!enabled);
        const bool clicked = IconButton(glyph, id, EditorButtonTone::Quiet, {}, TEXT::Localize(key).c_str());
        ImGui::EndDisabled();
        return clicked;
    };
    if (IconButton(EditorGlyph::Add, "##addNode", EditorButtonTone::Primary, {},
                   TEXT::Localize("ui.materialGraph.addNode").c_str())) {
        graphLinkCreatePending_ = false;
        ImGui::OpenPopup("##MaterialGraphAddNode");
    }
    if (button(EditorGlyph::Undo, "##undo", "ui.materialGraph.undo", CanUndo()))
        operation = "material.undo";
    if (button(EditorGlyph::Redo, "##redo", "ui.materialGraph.redo", CanRedo()))
        operation = "material.redo";
    if (button(EditorGlyph::Reveal, "##frameAll", "ui.materialGraph.frameAll"))
        requestFrameAll = true;
    if (button(EditorGlyph::Focus, "##focus", "ui.materialGraph.focusSelection", hasSelection))
        requestFocusSelection = true;
    if (button(EditorGlyph::Grid, "##layout", "material.layout.all", !graph.nodes.empty()))
        operation = "material.graph_layout";
    if (button(EditorGlyph::More, "##more", "ui.materialGraph.organize"))
        ImGui::OpenPopup("##MaterialGraphOrganize");
    if (ImGui::BeginPopup("##MaterialGraphOrganize")) {
        if (ImGui::MenuItem(TEXT::Localize("ui.materialGraph.copy").c_str(), "Ctrl+C", false, hasSelection))
            (void)CopySelectedGraphNodes(source, graph);
        paste = ImGui::MenuItem(TEXT::Localize("ui.materialGraph.paste").c_str(), "Ctrl+V", false,
                                !graphClipboard_.nodes.empty());
        if (ImGui::MenuItem(TEXT::Localize("ui.materialGraph.duplicate").c_str(), "Ctrl+D", false, hasSelection))
            operation = "material.duplicate_selected";
        ImGui::Separator();
        const auto item = [&](const char* key, const char* id, bool enabled = true) {
            if (ImGui::MenuItem(TEXT::Localize(key).c_str(), nullptr, false, enabled))
                operation = id;
        };
        item("material.layout.all", "material.graph_layout", !graph.nodes.empty());
        item("ui.materialGraph.arrangeSelected", "material.arrange_selected", hasSelection);
        item("ui.materialGraph.groupSelection", "material.group_selected", hasSelection);
        item("ui.materialGraph.ungroupSelected", "material.ungroup_selected", hasSelection);
        extract = ImGui::MenuItem(TEXT::Localize("material.function.extract").c_str(), nullptr, false, hasSelection);
        ImGui::Separator();
        item("ui.materialGraph.collapseSelected", "material.collapse_selected", hasSelection);
        item("ui.materialGraph.expandSelected", "material.expand_selected", hasSelection);
        item("ui.materialGraph.collapseAll", "material.collapse_all");
        item("ui.materialGraph.expandAll", "material.expand_all");
        ImGui::Separator();
        if (ImGui::MenuItem(TEXT::Localize("ui.materialGraph.deleteSelection").c_str(), "Delete", false,
                            hasSelection || !selectedGraphEdgeId_.empty())) {
            requestDeleteNode = hasSelection;
            requestDeleteEdge = !hasSelection;
        }
        ImGui::EndPopup();
    }
    if (extract)
        ImGui::OpenPopup("##ExtractMaterialFunction");
    if (ImGui::BeginPopup("##ExtractMaterialFunction")) {
        ImGui::InputText(TEXT::Localize("material.parameter.name").c_str(), functionName_.data(), functionName_.size());
        if (ImGui::Button(TEXT::Localize("material.function.extract").c_str()) && operationRegistry_) {
            const auto result =
                operationRegistry_->Execute("material.extract_function", {{"name", functionName_.data()}});
            statusMessage_ = result.message;
            if (result.ok)
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return true;
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##MaterialGraphAddNode")) {
        const MATH::Vec2 position{(360.0f - graphCanvas_.pan.x) / graphCanvas_.zoom,
                                  (180.0f - graphCanvas_.pan.y) / graphCanvas_.zoom};
        DrawMaterialGraphNodePalette(scene, source, graph, position);
        ImGui::EndPopup();
    }
    if (!operation.empty()) {
        RunMaterialOperation(operationRegistry_, operation, statusMessage_);
        return true;
    }
    if (paste) {
        (void)PasteGraphNodes(scene, source, graph, {30.0f, 30.0f});
        return true;
    }
    return false;
}

void ModelMaterialWorkspaceController::FinishMaterialGraphCanvasInteraction(
    DocumentSceneBase& scene, MaterialAssetData& source, MaterialGraphData& graph, std::string completedTargetNode,
    std::string completedTargetPin, bool linkReleased, bool mouseInsideCanvas, bool pointerOverNode,
    const std::string& hoveredNodeId, const std::string& hoveredEdgeId, MATH::Vec2 canvasOrigin, MATH::Vec2 pointer,
    bool requestDeleteNode, bool requestDeleteEdge) {
    const bool hasCompletedLink = !completedTargetNode.empty() && !completedTargetPin.empty();
    if (hasCompletedLink) {
        const auto edgeId =
            rewiredGraphEdgeId_.empty() ? "Edge_" + std::to_string(authoringIdCounter_++) : rewiredGraphEdgeId_;
        if (authoringDocument_.ConnectMaterialNodes(
                {edgeId, edgeSourceNodeId_, edgeSourcePin_, completedTargetNode, completedTargetPin}, statusMessage_)) {
            selectedGraphNodeId_.clear();
            selectedGraphEdgeId_ = FindMaterialGraphInputEdge(graph, completedTargetNode, completedTargetPin)->id;
        }
    } else if (linkReleased && !rewiredGraphEdgeId_.empty() && mouseInsideCanvas) {
        if (authoringDocument_.DisconnectMaterialEdge(rewiredGraphEdgeId_))
            selectedGraphEdgeId_.clear();
    }
    if (linkReleased && !hasCompletedLink && rewiredGraphEdgeId_.empty() && !edgeSourceNodeId_.empty() &&
        mouseInsideCanvas && !pointerOverNode) {
        graphLinkCreatePending_ = true;
        graphSearch_.fill(0);
        graphContextPosition_ = NodeGraphCanvas::ScreenToCanvas(graphCanvas_, canvasOrigin, pointer);
        ImGui::OpenPopup("##MaterialGraphCanvasContext");
    }
    if (linkReleased) {
        graphLinkDragActive_ = false;
        rewiredGraphEdgeId_.clear();
        if (!graphLinkCreatePending_) {
            edgeSourceNodeId_.clear();
            edgeSourcePin_.clear();
        }
        edgeTargetNodeId_.clear();
        edgeTargetPin_.clear();
    }

    if (mouseInsideCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        graphLinkCreatePending_ = false;
        if (pointerOverNode) {
            SetGraphNodeSelection(hoveredNodeId, ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift);
            selectedGraphEdgeId_.clear();
            ImGui::OpenPopup("##MaterialGraphSelectionContext");
        } else if (!hoveredEdgeId.empty()) {
            ClearGraphNodeSelection();
            selectedGraphEdgeId_ = hoveredEdgeId;
            ImGui::OpenPopup("##MaterialGraphSelectionContext");
        } else {
            ClearGraphNodeSelection();
            selectedGraphEdgeId_.clear();
            graphContextPosition_ = NodeGraphCanvas::ScreenToCanvas(graphCanvas_, canvasOrigin, pointer);
            ImGui::OpenPopup("##MaterialGraphCanvasContext");
        }
    }
    if (graphLinkCreatePending_ && !ImGui::IsPopupOpen("##MaterialGraphCanvasContext")) {
        graphLinkCreatePending_ = false;
        edgeSourceNodeId_.clear();
        edgeSourcePin_.clear();
    }
    if (ImGui::BeginPopup("##MaterialGraphCanvasContext")) {
        if (ImGui::MenuItem(TEXT::Localize("material.layout.all").c_str())) {
            RunMaterialOperation(operationRegistry_, "material.graph_layout", statusMessage_);
            ImGui::EndPopup();
            return;
        }
        ImGui::Separator();
        DrawMaterialGraphNodePalette(scene, source, graph, graphContextPosition_);
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##MaterialGraphSelectionContext")) {
        if (!selectedGraphNodeId_.empty()) {
            if (ImGui::MenuItem(TEXT::Localize("ui.materialGraph.copy").c_str(), "Ctrl+C")) {
                (void)CopySelectedGraphNodes(source, graph);
            }
            if (ImGui::MenuItem(TEXT::Localize("ui.materialGraph.duplicate").c_str(), "Ctrl+D")) {
                RunMaterialOperation(operationRegistry_, "material.duplicate_selected", statusMessage_);
                ImGui::EndPopup();
                return;
            }
            if (ImGui::MenuItem(TEXT::Localize("ui.materialGraph.arrangeSelected").c_str())) {
                RunMaterialOperation(operationRegistry_, "material.arrange_selected", statusMessage_);
                ImGui::EndPopup();
                return;
            }
            ImGui::Separator();
            const MaterialGraphNodeData* node = FindMaterialGraphNode(graph, selectedGraphNodeId_);
            const bool isOutput = node != nullptr && node->kind == MaterialNodeKind::SurfaceOutput;
            const bool multiSelection = selectedGraphNodeIds_.size() > 1u;
            ImGui::BeginDisabled(isOutput);
            requestDeleteNode = ImGui::MenuItem(
                TEXT::Localize(multiSelection ? "ui.materialGraph.deleteSelected" : "ui.materialGraph.deleteNode")
                    .c_str(),
                "Delete");
            ImGui::EndDisabled();
            if (node != nullptr) {
                const bool collapsed = collapsedGraphNodeIds_.contains(node->id);
                if (ImGui::MenuItem(
                        TEXT::Localize(collapsed ? "ui.materialGraph.expandNode" : "ui.materialGraph.collapseNode")
                            .c_str())) {
                    RunMaterialOperation(operationRegistry_,
                                         collapsed ? "material.expand_selected" : "material.collapse_selected",
                                         statusMessage_);
                }
            }
            if (isOutput) {
                ImGui::TextDisabled("%s", TEXT::Localize("ui.materialGraph.surfaceOutputRoot").c_str());
            }
        } else if (!selectedGraphEdgeId_.empty()) {
            requestDeleteEdge = ImGui::MenuItem(TEXT::Localize("ui.materialGraph.deleteConnection").c_str(), "Delete");
        }
        ImGui::EndPopup();
    }

    ImGuiIO& io = ImGui::GetIO();
    const bool hasNodeSelection = !selectedGraphNodeIds_.empty() || !selectedGraphNodeId_.empty();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput) {
        if (io.KeyCtrl && ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_A, true, false)) {
            selectedGraphNodeIds_.clear();
            for (const auto& node : graph.nodes)
                selectedGraphNodeIds_.push_back(node.id);
            selectedGraphNodeId_ = graph.nodes.empty() ? "" : graph.nodes.front().id;
        }
        if (io.KeyCtrl && ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_C,
                                                ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows), false)) {
            (void)CopySelectedGraphNodes(source, graph);
        }
        if (io.KeyCtrl && ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_V,
                                                ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows), false)) {
            (void)PasteGraphNodes(scene, source, graph, MATH::Vec2{30.0f, 30.0f});
        }
        if (io.KeyCtrl && ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_D,
                                                ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows), false)) {
            RunMaterialOperation(operationRegistry_, "material.duplicate_selected", statusMessage_);
        }
        if (ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_Delete,
                                  ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows), false)) {
            requestDeleteNode = hasNodeSelection;
            requestDeleteEdge = !requestDeleteNode && !selectedGraphEdgeId_.empty();
        }
    }
    if (requestDeleteNode) {
        RunMaterialOperation(operationRegistry_, "material.delete_selected", statusMessage_);
        graphLinkDragActive_ = false;
        rewiredGraphEdgeId_.clear();
        draggedGraphNodeId_.clear();
        graphNodeDragStarted_ = false;
        requestDeleteNode = false;
    } else if (requestDeleteEdge && !selectedGraphEdgeId_.empty()) {
        RunMaterialOperation(operationRegistry_, "material.delete_connection", statusMessage_);
        requestDeleteEdge = false;
    }
}

} // namespace HIKARI::EDITOR
