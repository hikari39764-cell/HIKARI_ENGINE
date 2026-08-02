#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

bool ModelMaterialWorkspaceController::IsPreviewReady() const noexcept {
  return GetActivePreviewScene().IsReady();
}

ModelPreviewScene &
ModelMaterialWorkspaceController::GetActivePreviewScene() noexcept {
  return authoringMode_ == AuthoringMode::MaterialAsset
             ? materialSphereScene_
             : previewScene_;
}

const ModelPreviewScene &
ModelMaterialWorkspaceController::GetActivePreviewScene() const noexcept {
  return authoringMode_ == AuthoringMode::MaterialAsset
             ? materialSphereScene_
             : previewScene_;
}

bool ModelMaterialWorkspaceController::EnsureMaterialSpherePreview(
    DocumentSceneBase &scene, std::string &outMessage) {
  InitializePreviewLighting();
  materialSphereScene_.SetRenderPolicy(ModelPreviewRenderPolicy{
      .castShadow = true,
      .receiveShadow = true,
      .allowStaticCachedForward = true,
  });
  if (materialSphereScene_.IsReady()) {
    return true;
  }
  return materialSphereScene_.LoadMaterialSphere(
      scene.GetAssetDatabase().GetProjectRoot(), outMessage);
}

bool ModelMaterialWorkspaceController::SwitchAuthoringMode(
    DocumentSceneBase &scene, AuthoringMode mode,
    std::string &outMessage) {
  if (mode == authoringMode_) {
    return mode != AuthoringMode::MaterialAsset ||
           EnsureMaterialSpherePreview(scene, outMessage);
  }
  if (!executingPendingTransition_ &&
      (authoringDocument_.IsMaterialDirty() ||
       authoringDocument_.IsBindingDirty() ||
       authoringDocument_.IsAbPreviewActive())) {
    PendingTransition transition{};
    transition.kind = PendingTransitionKind::SwitchAuthoringMode;
    transition.mode = mode;
    QueuePendingTransition(std::move(transition), outMessage);
    return false;
  }

  SaveActiveCameraState();
  authoringMode_ = mode;
  selectedPreviewLightIndex_.reset();
  cameraCutPending_ = true;
  if (mode == AuthoringMode::MaterialAsset) {
    if (!EnsureMaterialSpherePreview(scene, outMessage)) {
      authoringMode_ = AuthoringMode::ModelMaterials;
      return false;
    }
    FitPreviewCamera();
    if (authoringDocument_.HasMaterial()) {
      (void)RebuildAuthoredPreview(scene.GetAssetDatabase(),
                                   scene.GetAssetRegistry(), outMessage);
    }
    outMessage = "standalone Material Asset workflow ready";
    return true;
  }

  if (!IsEditingModel()) {
    outMessage = "Model Materials workflow ready; choose a model";
    return true;
  }
  FitPreviewCamera();
  if (selectedMaterialIndex_.has_value()) {
    return SelectSlotForEditing(scene, *selectedMaterialIndex_, outMessage);
  }
  outMessage = "Model Materials workflow ready";
  return true;
}

bool ModelMaterialWorkspaceController::SelectSlotForEditing(
    DocumentSceneBase &scene, uint32_t materialIndex,
    std::string &outMessage) {
  if (authoringMode_ != AuthoringMode::ModelMaterials &&
      !SwitchAuthoringMode(scene, AuthoringMode::ModelMaterials,
                           outMessage)) {
    return false;
  }
  if (selectedMaterialIndex_ != materialIndex &&
      !executingPendingTransition_ &&
      (authoringDocument_.IsMaterialDirty() ||
       authoringDocument_.IsAbPreviewActive())) {
    PendingTransition transition{};
    transition.kind = PendingTransitionKind::SelectMaterialSlot;
    transition.materialIndex = materialIndex;
    QueuePendingTransition(std::move(transition), outMessage);
    return false;
  }
  SlotState *slot = FindSlotByMaterialIndex(materialIndex);
  if (slot == nullptr) {
    outMessage = "selected model part is no longer available";
    return false;
  }
  if (selectedMaterialIndex_.has_value() &&
      *selectedMaterialIndex_ != materialIndex &&
      authoringDocument_.HasMaterial() &&
      !authoringDocument_.GetMaterialAssetGuid().IsValid()) {
    if (SlotState *previous =
            FindSlotByMaterialIndex(*selectedMaterialIndex_)) {
      (void)previewScene_.ClearPreviewMaterialOverride(
          previous->view.materialIndex);
      previous->previewMaterial.reset();
      previous->view.source =
          ModelMaterialBindingSource::ImportedFallback;
      previous->view.diagnostic.clear();
    }
  }
  selectedMaterialIndex_ = materialIndex;
  selectedPreviewLightIndex_.reset();

  const bool opened =
      slot->view.overrideMaterialGuid.IsValid() &&
              slot->view.source ==
                  ModelMaterialBindingSource::AuthoredOverride
          ? OpenMaterialAsset(scene, slot->view.overrideMaterialGuid,
                              outMessage)
          : OpenImportedMaterialDraft(scene, materialIndex, outMessage);
  if (opened) {
    RefreshSelectedSlotPresentation();
  }
  return opened;
}

} // namespace HIKARI::EDITOR
