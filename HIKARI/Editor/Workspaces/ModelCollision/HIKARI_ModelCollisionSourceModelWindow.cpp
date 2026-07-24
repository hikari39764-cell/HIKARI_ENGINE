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
  if (!ImGui::Begin("Source Model###ModelCollision/Source")) {
    ImGui::End();
    return;
  }
  const ModelAsset *model = previewScene_.GetModel();
  if (model == nullptr) {
    ImGui::TextDisabled("No model loaded.");
    ImGui::End();
    return;
  }
  const std::vector<ModelCollisionPreviewNode> &sourceNodes =
      previewScene_.GetSourceNodes();
  ImGui::Text("%d mesh nodes | %d selected",
              static_cast<int>(sourceNodes.size()),
              static_cast<int>(selectedSourceNodes_.size()));
  ImGui::TextDisabled("%d meshes", static_cast<int>(model->meshes.size()));
  ImGui::InputTextWithHint("##NodeSearch", "Filter nodes", sourceSearch_.data(),
                           sourceSearch_.size());
  ImGui::SameLine();
  if (ImGui::SmallButton("Select Filtered")) {
    const std::string_view query(sourceSearch_.data());
    for (const ModelCollisionPreviewNode &node : sourceNodes) {
      if (MODEL_COLLISION_PRESENTATION::MatchesSearch(node.name, query)) {
        selectedSourceNodes_.insert(node.nodeIndex);
        primarySourceNodeIndex_ = node.nodeIndex;
      }
    }
    selectionMode_ = ModelCollisionSelectionMode::SourceNodes;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Clear Selection")) {
    selectedSourceNodes_.clear();
    primarySourceNodeIndex_ = -1;
  }
  if (!selectedSourceNodes_.empty() && ImGui::Button("Frame Selected")) {
    selectionMode_ = ModelCollisionSelectionMode::SourceNodes;
    FocusSelection();
  }
  ImGui::Separator();
  if (ImGui::BeginChild("##SourceNodeList")) {
    const std::string_view query(sourceSearch_.data());
    for (const ModelCollisionPreviewNode &node : sourceNodes) {
      if (!MODEL_COLLISION_PRESENTATION::MatchesSearch(node.name, query)) {
        continue;
      }
      ImGui::PushID(node.nodeIndex);
      const std::string label =
          node.name + "  [mesh " + std::to_string(node.meshIndex) + "]";
      if (ImGui::Selectable(label.c_str(),
                            selectedSourceNodes_.contains(node.nodeIndex),
                            ImGuiSelectableFlags_AllowDoubleClick)) {
        const ImGuiIO &io = ImGui::GetIO();
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
