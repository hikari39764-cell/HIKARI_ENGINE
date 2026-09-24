#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionWorkspaceController.h"

#include <string_view>

#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionPresentation.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

void ModelCollisionWorkspaceController::DrawSourceModelWindow() {
#if defined(HIKARI_WITH_EDITOR)
    if (!ImGui::Begin(TEXT::LocalizeWindow("editor.ui.source.model.acf996", "ModelCollision/Source").c_str())) {
        ImGui::End();
        return;
    }
    const ModelAsset* model = previewScene_.GetModel();
    if (model == nullptr) {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.no.model.loaded.bdddc1").c_str());
        ImGui::End();
        return;
    }
    const std::vector<ModelPreviewNode>& sourceNodes = previewScene_.GetSourceNodes();
    ImGui::Text(TEXT::Localize("editor.ui.d.mesh.nodes.d.selected.a86e1e").c_str(),
                static_cast<int>(sourceNodes.size()), static_cast<int>(selectedSourceNodes_.size()));
    ImGui::TextDisabled(TEXT::Localize("editor.ui.d.meshes.da2974").c_str(), static_cast<int>(model->meshes.size()));
    ImGui::InputTextWithHint("##NodeSearch", TEXT::Localize("editor.ui.filter.nodes.0b8a25").c_str(),
                             sourceSearch_.data(), sourceSearch_.size());
    ImGui::SameLine();
    if (ImGui::SmallButton(TEXT::LocalizeWindow("editor.ui.select.filtered.2b2f4e", "Select Filtered").c_str())) {
        const std::string_view query(sourceSearch_.data());
        for (const ModelPreviewNode& node : sourceNodes) {
            if (MODEL_COLLISION_PRESENTATION::MatchesSearch(node.name, query)) {
                selectedSourceNodes_.insert(node.nodeIndex);
                primarySourceNodeIndex_ = node.nodeIndex;
            }
        }
        selectionMode_ = ModelCollisionSelectionMode::SourceNodes;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(TEXT::LocalizeWindow("editor.ui.clear.selection.fae58c", "Clear Selection").c_str())) {
        selectedSourceNodes_.clear();
        primarySourceNodeIndex_ = -1;
    }
    if (!selectedSourceNodes_.empty() &&
        ImGui::Button(TEXT::LocalizeWindow("editor.ui.frame.selected.3f6ad6", "Frame Selected").c_str())) {
        selectionMode_ = ModelCollisionSelectionMode::SourceNodes;
        FocusSelection();
    }
    ImGui::Separator();
    if (ImGui::BeginChild("##SourceNodeList")) {
        const std::string_view query(sourceSearch_.data());
        for (const ModelPreviewNode& node : sourceNodes) {
            if (!MODEL_COLLISION_PRESENTATION::MatchesSearch(node.name, query)) {
                continue;
            }
            ImGui::PushID(node.nodeIndex);
            const std::string label = node.name + "  [mesh " + std::to_string(node.meshIndex) + "]";
            if (ImGui::Selectable(label.c_str(), selectedSourceNodes_.contains(node.nodeIndex),
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                const ImGuiIO& io = ImGui::GetIO();
                selectionMode_ = ModelCollisionSelectionMode::SourceNodes;
                SelectSourceNode(node.nodeIndex, io.KeyCtrl || io.KeyShift);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    FocusSelection();
                }
            }
            if (sourceNodeScrollRequest_ == node.nodeIndex) {
                ImGui::SetScrollHereY(0.5f);
                sourceNodeScrollRequest_ = -1;
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::End();
#endif
}

} // namespace HIKARI::EDITOR
