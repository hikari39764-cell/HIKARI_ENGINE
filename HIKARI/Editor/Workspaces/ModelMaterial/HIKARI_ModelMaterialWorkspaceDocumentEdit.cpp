#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceDocument.h"

#include "Assets/Material/Serialization/HIKARI_MaterialSourceJson.h"
#include "Assets/Material/Graph/HIKARI_MaterialGraphValidation.h"
#include "Assets/Material/Graph/HIKARI_MaterialGraphQuery.h"

#include <string>
#include <utility>
#include <unordered_set>

#include "Assets/Material/HIKARI_MaterialAssetData.h"

namespace HIKARI::EDITOR {

MaterialGraphData* ModelMaterialWorkspaceDocument::EditableGraph() noexcept {
    if (!materialOpen_ || HasMaterialGesture())
        return nullptr;
    return material_.ActiveGraph();
}

bool ModelMaterialWorkspaceDocument::ConnectMaterialNodes(MaterialGraphEdgeData edge, std::string& outMessage) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto* graph = EditableGraph();
    if (!graph || !IsValidMaterialStableId(edge.id)) {
        outMessage = "Open a graph, finish the active edit and supply a stable edge ID.";
        return false;
    }
    if (!CanConnectMaterialGraphPins(*graph, edge.sourceNodeId, edge.sourcePin, edge.targetNodeId, edge.targetPin,
                                     outMessage))
        return false;
    const auto* existing = FindMaterialGraphInputEdge(*graph, edge.targetNodeId, edge.targetPin);
    const bool rewiring = std::any_of(graph->edges.begin(), graph->edges.end(),
                                      [&](const auto& current) { return current.id == edge.id; });
    if (existing && existing->sourceNodeId == edge.sourceNodeId && existing->sourcePin == edge.sourcePin &&
        (!rewiring || existing->id == edge.id))
        return true;
    auto before = material_;
    std::erase_if(graph->edges, [&](const auto& current) {
        return current.id == edge.id ||
               (current.targetNodeId == edge.targetNodeId && current.targetPin == edge.targetPin);
    });
    graph->edges.push_back(std::move(edge));
    return CommitMaterialLocked(std::move(before), "Connect material graph nodes");
}

bool ModelMaterialWorkspaceDocument::RemoveMaterialNodes(std::span<const std::string> ids) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto* graph = EditableGraph();
    if (!graph || ids.empty())
        return false;
    auto before = material_;
    const std::unordered_set<std::string> selected(ids.begin(), ids.end());
    const auto removed = std::erase_if(graph->nodes, [&](const auto& node) {
        return node.kind != MaterialNodeKind::SurfaceOutput && selected.contains(node.id);
    });
    if (!removed)
        return false;
    std::unordered_set<std::string> remaining;
    for (const auto& node : graph->nodes)
        remaining.insert(node.id);
    std::erase_if(graph->edges, [&](const auto& edge) {
        return !remaining.contains(edge.sourceNodeId) || !remaining.contains(edge.targetNodeId);
    });
    return CommitMaterialLocked(std::move(before), "Delete material graph nodes");
}

bool ModelMaterialWorkspaceDocument::DisconnectMaterialEdge(std::string_view id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto* graph = EditableGraph();
    if (!graph)
        return false;
    auto before = material_;
    if (!std::erase_if(graph->edges, [&](const auto& edge) { return edge.id == id; }))
        return false;
    return CommitMaterialLocked(std::move(before), "Disconnect material graph edge");
}

bool ModelMaterialWorkspaceDocument::AreMaterialContentsEquivalent(const MaterialAssetData& left,
                                                                   const MaterialAssetData& right) {
    std::string leftText, rightText, error;
    return MATERIAL_SERIALIZATION::SerializeMaterialCompileInputJson(left, leftText, error) &&
           MATERIAL_SERIALIZATION::SerializeMaterialCompileInputJson(right, rightText, error) && leftText == rightText;
}

bool ModelMaterialWorkspaceDocument::ConsumeMaterialPreviewChange() noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return std::exchange(materialPreviewPending_, false);
}

void ModelMaterialWorkspaceDocument::RefreshAfterMaterialChange(bool contentChanged) {
    const bool wasCurrent = compileRevision_ == materialRevision_;
    ++materialRevision_;
    if (contentChanged) {
        RefreshCompile();
    } else if (wasCurrent) {
        compileRevision_ = materialRevision_;
    }
}

bool ModelMaterialWorkspaceDocument::CommitMaterialLocked(MaterialAssetData before, std::string label) {
    if (!materialOpen_)
        return false;
    MaterialGraphData* graph = material_.kind == MaterialAssetKind::Material   ? &material_.graph
                               : material_.kind == MaterialAssetKind::Function ? &material_.function.graph
                                                                               : nullptr;
    if (graph) {
        const auto& oldGraph = before.kind == MaterialAssetKind::Function ? before.function.graph : before.graph;
        // UI と外部コマンドのどちらで消したノードも、グループに参照を残さない。
        for (auto& group : graph->groups)
            std::erase_if(group.nodeIds, [&](const auto& id) {
                return FindMaterialGraphNode(oldGraph, id) && !FindMaterialGraphNode(*graph, id);
            });
        std::erase_if(graph->groups, [](const auto& group) { return group.nodeIds.empty(); });
        (void)ReconcileMaterialGraphDynamicNodeTypes(*graph);
    }
    if (AreMaterialsEquivalent(before, material_))
        return false;
    const bool contentChanged = !AreMaterialContentsEquivalent(before, material_);
    if (contentChanged)
        SyncPbrFromGraphParameters(material_);
    if (previewEdit_) {
        RefreshAfterMaterialChange(contentChanged);
        return true;
    }
    RecordMaterialChange(before, material_, std::move(label));
    RefreshAfterMaterialChange(contentChanged);
    return true;
}

void ModelMaterialWorkspaceDocument::RecordMaterialChange(const MaterialAssetData& before,
                                                          const MaterialAssetData& after, std::string label) {
    if (AreMaterialsEquivalent(before, after))
        return;
    const auto domain =
        AreMaterialContentsEquivalent(before, after) ? ChangedDomain::MaterialLayout : ChangedDomain::Material;
    materialHistory_.Record(before, after, 0, {domain, std::move(label)});
    activeHistoryDomain_ = ChangedDomain::Material;
}

void ModelMaterialWorkspaceDocument::UpdateMaterialItem(MaterialAssetData before, std::string label, uint32_t itemId,
                                                        bool active, bool changed) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!materialOpen_)
        return;
    if (HasMaterialGesture() && previewEdit_->itemId != itemId && (active || changed)) {
        // 新しい項目の変更前で前のジェスチャーを閉じる。UI の参照は無効化しない。
        RecordMaterialChange(previewEdit_->before, before, std::move(previewEdit_->label));
        previewEdit_.reset();
    }
    if (!previewEdit_ && active)
        previewEdit_ = PreviewEdit{before, label, false, itemId};
    if (changed)
        CommitMaterialLocked(std::move(before), std::move(label));
    if (HasMaterialGesture() && previewEdit_->itemId == itemId && !active)
        EndMaterialGesture(false);
}

void ModelMaterialWorkspaceDocument::FinishMaterialItems(uint32_t activeItem) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (HasMaterialGesture() && previewEdit_->itemId != 0 && previewEdit_->itemId != activeItem)
        EndMaterialGesture(false);
}

bool ModelMaterialWorkspaceDocument::ApplyMaterial(MaterialAssetData source, std::string label) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!materialOpen_ || HasMaterialGesture())
        return false;
    auto before = std::move(material_);
    material_ = std::move(source);
    return CommitMaterialLocked(std::move(before), std::move(label));
}

void ModelMaterialWorkspaceDocument::BeginMaterialGesture(MaterialAssetData before, std::string label) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (materialOpen_ && !previewEdit_)
        previewEdit_ = PreviewEdit{std::move(before), std::move(label), false};
}

bool ModelMaterialWorkspaceDocument::HasMaterialGesture() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return previewEdit_ && !previewEdit_->audition;
}

bool ModelMaterialWorkspaceDocument::EndMaterialGesture(bool cancel) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!HasMaterialGesture())
        return false;
    auto edit = std::move(*previewEdit_);
    previewEdit_.reset();
    const bool changed = !AreMaterialsEquivalent(edit.before, material_);
    if (!cancel) {
        if (edit.itemId == 0)
            return CommitMaterialLocked(std::move(edit.before), std::move(edit.label));
        // 項目の変更は既にコンパイル・通知済み。終了時は履歴だけを確定する。
        RecordMaterialChange(edit.before, material_, std::move(edit.label));
        return changed;
    }
    RestoreMaterial(std::move(edit.before));
    return changed;
}

} // namespace HIKARI::EDITOR
