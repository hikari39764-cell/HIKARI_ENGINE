#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <string>
#include <utility>

#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

void ModelMaterialWorkspaceController::FlushPendingMaterialPreview(
    DocumentSceneBase &scene) {
  if (!materialPreviewRefreshPending_) {
    return;
  }
  materialPreviewRefreshPending_ = false;
  (void)RefreshMaterialPreviews(scene, statusMessage_);
}

void ModelMaterialWorkspaceController::BeginMaterialGesture(
    std::string label) {
  if (!authoringDocument_.HasMaterial() || materialGesture_) {
    return;
  }
  materialGesture_ = MaterialEditGesture{authoringDocument_.GetMaterial(),
                                         std::move(label)};
}

void ModelMaterialWorkspaceController::PreviewMaterialGesture(
    DocumentSceneBase &scene) {
  (void)scene;
  if (!materialGesture_ || !authoringDocument_.HasMaterial()) {
    return;
  }
  const MaterialAssetKind kind = authoringDocument_.GetMaterial().kind;
  if (kind != MaterialAssetKind::Material &&
      kind != MaterialAssetKind::Instance) {
    return;
  }
  // Drag/color gestures may emit multiple changes in one ImGui frame. Publish
  // only the final authored state once after all workspace panels have drawn.
  materialPreviewRefreshPending_ = true;
}

void ModelMaterialWorkspaceController::CommitMaterialGesture(
    DocumentSceneBase &scene) {
  if (!materialGesture_) {
    return;
  }
  MaterialEditGesture gesture = std::move(*materialGesture_);
  materialGesture_.reset();
  materialPreviewRefreshPending_ = false;
  if (authoringDocument_.CommitMaterial(std::move(gesture.before),
                                        std::move(gesture.label))) {
    (void)RefreshMaterialPreviews(scene, statusMessage_);
  }
}

void ModelMaterialWorkspaceController::CancelMaterialGesture(
    DocumentSceneBase &scene) {
  if (!materialGesture_) {
    return;
  }
  authoringDocument_.RestoreMaterial(std::move(materialGesture_->before));
  materialGesture_.reset();
  materialPreviewRefreshPending_ = false;
  (void)RefreshMaterialPreviews(scene, statusMessage_);
  statusMessage_ = "material edit canceled";
}

void ModelMaterialWorkspaceController::CommitMaterialMutation(
    MaterialAssetData before, std::string label,
    DocumentSceneBase &scene) {
  if (authoringDocument_.CommitMaterial(std::move(before), std::move(label))) {
    (void)RefreshMaterialPreviews(scene, statusMessage_);
  }
}

bool ModelMaterialWorkspaceController::ApplyHistoryProjection(
    ModelMaterialWorkspaceDocument::ChangedDomain domain,
    DocumentSceneBase &scene,
    std::string &outMessage) {
  if (domain == ModelMaterialWorkspaceDocument::ChangedDomain::Binding) {
    RebuildSlotState(scene.GetAssetDatabase(), scene.GetAssetRegistry());
    return true;
  }
  if (domain == ModelMaterialWorkspaceDocument::ChangedDomain::Material) {
    return RefreshMaterialPreviews(scene, outMessage);
  }
  return true;
}

} // namespace HIKARI::EDITOR
