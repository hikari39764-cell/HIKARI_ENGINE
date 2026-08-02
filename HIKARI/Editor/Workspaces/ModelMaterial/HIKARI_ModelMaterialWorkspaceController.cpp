#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Assets/Material/Authoring/HIKARI_MaterialAssetAuthoringService.h"
#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Assets/Models/MaterialBindings/HIKARI_ModelMaterialBindingAsset.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"

namespace HIKARI::EDITOR {

void ModelMaterialWorkspaceController::ApplyWorkspaceActivation(
    DocumentSceneBase &scene, const EditorWorkspaceActivation &activation,
    EditorWorkspaceHost &workspaceHost) {
  if (activation.previous == EditorWorkspaceId::ModelMaterial &&
      activation.current != EditorWorkspaceId::ModelMaterial) {
    if (materialGesture_) {
      CancelMaterialGesture(scene);
    }
    materialPreviewRefreshPending_ = false;
    SaveActiveCameraState();
    ClosePreviewRequest(workspaceHost);
    return;
  }
  if (activation.current != EditorWorkspaceId::ModelMaterial) {
    return;
  }

  EnsureBuiltInRampPresets(scene);

  if (activation.materialAssetGuid.has_value() &&
      activation.materialAssetGuid->IsValid()) {
    candidateMaterialGuid_ = *activation.materialAssetGuid;
    if (SwitchAuthoringMode(scene, AuthoringMode::MaterialAsset,
                            statusMessage_)) {
      (void)OpenMaterialAsset(scene, *activation.materialAssetGuid,
                              statusMessage_);
    }
  }
  if (activation.modelAssetGuid.has_value()) {
    authoringMode_ = AuthoringMode::ModelMaterials;
    (void)OpenModel(scene, *activation.modelAssetGuid, statusMessage_);
  } else if (IsEditingModel()) {
    cameraCutPending_ = true;
  }
}

void ModelMaterialWorkspaceController::BindWorkspaceCommands(
    EditorCommandRouter &commandRouter, DocumentSceneBase &scene) {
  commandRouter.Bind(EditorCommandId::FocusSelection, "Frame Material Model",
                     IsPreviewReady(), [this]() { FitPreviewCamera(); });
  std::string undoLabel = "Undo Material Edit";
  if (const std::string *label = authoringDocument_.GetUndoLabel()) {
    undoLabel = "Undo " + *label;
  }
  std::string redoLabel = "Redo Material Edit";
  if (const std::string *label = authoringDocument_.GetRedoLabel()) {
    redoLabel = "Redo " + *label;
  }
  commandRouter.Bind(EditorCommandId::Undo, std::move(undoLabel), CanUndo(),
                     [this, &scene]() { (void)Undo(scene, statusMessage_); });
  commandRouter.Bind(EditorCommandId::Redo, std::move(redoLabel), CanRedo(),
                     [this, &scene]() { (void)Redo(scene, statusMessage_); });
  commandRouter.Bind(
      EditorCommandId::SaveDocument, "Save Model Materials",
      authoringDocument_.HasBindings() || authoringDocument_.HasMaterial(),
      [this, &scene]() {
        if (authoringDocument_.IsMaterialDirty() &&
            !SaveMaterial(scene, false, statusMessage_)) {
          return;
        }
        if (authoringDocument_.IsBindingDirty()) {
          (void)SaveBindings(scene, false, statusMessage_);
        } else if (!authoringDocument_.IsMaterialDirty()) {
          statusMessage_ = "material workspace is already saved";
        }
      });
}

bool ModelMaterialWorkspaceController::CanUndo() const noexcept {
  return authoringDocument_.CanUndo();
}

bool ModelMaterialWorkspaceController::CanRedo() const noexcept {
  return authoringDocument_.CanRedo();
}

bool ModelMaterialWorkspaceController::Undo(DocumentSceneBase &scene,
                                            std::string &outMessage) {
  ModelMaterialWorkspaceDocument::ChangedDomain domain{};
  std::string label{};
  if (!authoringDocument_.Undo(domain, label)) {
    outMessage = authoringDocument_.IsAbPreviewActive()
                     ? "Keep or Discard the A/B preview before undoing"
                     : "nothing to undo";
    return false;
  }
  if (!ApplyHistoryProjection(domain, scene, outMessage)) {
    return false;
  }
  outMessage = "undid " + label;
  return true;
}

bool ModelMaterialWorkspaceController::Redo(DocumentSceneBase &scene,
                                            std::string &outMessage) {
  ModelMaterialWorkspaceDocument::ChangedDomain domain{};
  std::string label{};
  if (!authoringDocument_.Redo(domain, label)) {
    outMessage = authoringDocument_.IsAbPreviewActive()
                     ? "Keep or Discard the A/B preview before redoing"
                     : "nothing to redo";
    return false;
  }
  if (!ApplyHistoryProjection(domain, scene, outMessage)) {
    return false;
  }
  outMessage = "redid " + label;
  return true;
}

bool ModelMaterialWorkspaceController::IsEditingModel() const noexcept {
  return modelGuid_.IsValid() && previewScene_.IsReady();
}

const AssetGuid &ModelMaterialWorkspaceController::GetModelGuid() const noexcept {
  return modelGuid_;
}

const std::vector<ModelMaterialSlotView> &
ModelMaterialWorkspaceController::GetSlotViews() const noexcept {
  return publicSlotViews_;
}

const ModelMaterialSlotView *
ModelMaterialWorkspaceController::GetSelectedSlot() const noexcept {
  if (!selectedMaterialIndex_.has_value()) {
    return nullptr;
  }
  const auto found = std::find_if(
      publicSlotViews_.begin(), publicSlotViews_.end(),
      [this](const ModelMaterialSlotView &slot) {
        return slot.materialIndex == *selectedMaterialIndex_;
      });
  return found != publicSlotViews_.end() ? &*found : nullptr;
}

const ModelMaterialWorkspaceStats &
ModelMaterialWorkspaceController::GetStats() const noexcept {
  return stats_;
}

bool ModelMaterialWorkspaceController::SelectSlot(
    uint32_t materialIndex) noexcept {
  if (FindSlotByMaterialIndex(materialIndex) == nullptr) {
    return false;
  }
  selectedMaterialIndex_ = materialIndex;
  return true;
}

bool ModelMaterialWorkspaceController::OpenModel(DocumentSceneBase &scene,
                                                 const AssetGuid &guid,
                                                 std::string &outMessage) {
  if (!guid.IsValid()) {
    outMessage = "select a valid model asset";
    return false;
  }
  if (guid == modelGuid_ && previewScene_.IsReady()) {
    outMessage = "model is already open";
    return true;
  }
  if (guid != modelGuid_ &&
      !executingPendingTransition_ &&
      (authoringDocument_.IsBindingDirty() ||
       authoringDocument_.IsMaterialDirty() ||
       authoringDocument_.IsAbPreviewActive())) {
    PendingTransition transition{};
    transition.kind = PendingTransitionKind::OpenModel;
    transition.assetGuid = guid;
    QueuePendingTransition(std::move(transition), outMessage);
    return false;
  }

  AssetDatabase &assetDatabase = scene.GetAssetDatabase();
  const AssetRecord *record = assetDatabase.FindByGuid(guid);
  if (record == nullptr || record->type != AssetType::Model) {
    outMessage = "selected model is no longer available";
    return false;
  }

  SaveActiveCameraState();
  authoringMode_ = AuthoringMode::ModelMaterials;
  InitializePreviewLighting();
  previewScene_.SetRenderPolicy(ModelPreviewRenderPolicy{
      .castShadow = true,
      .receiveShadow = true,
      .allowStaticCachedForward = true,
  });
  slots_.clear();
  publicSlotViews_.clear();
  selectedMaterialIndex_.reset();
  stats_ = {};
  materialPreviewRefreshPending_ = false;
  modelGuid_ = {};
  modelDisplayName_.clear();
  if (!previewScene_.Load(*record, assetDatabase.GetProjectRoot(), outMessage)) {
    return false;
  }

  modelGuid_ = guid;
  modelDisplayName_ = GetAssetRecordDisplayName(*record);
  std::string bindingMessage{};
  (void)LoadOrCreateBindings(scene, bindingMessage);
  RebuildSlotState(assetDatabase, scene.GetAssetRegistry());
  if (!slots_.empty()) {
    selectedMaterialIndex_ = slots_.front().view.materialIndex;
  }

  if (cameraStateStore_.Restore(EditorWorkspaceId::ModelMaterial, modelGuid_,
                                cameraController_)) {
    ++cameraRevision_;
    cameraCutPending_ = true;
  } else {
    FitPreviewCamera();
  }

  const size_t unavailableCount = static_cast<size_t>(std::count_if(
      slots_.begin(), slots_.end(), [](const SlotState &slot) {
        return slot.view.source ==
               ModelMaterialBindingSource::OverrideUnavailable;
      }));
  outMessage = "model material workspace opened with " +
               std::to_string(slots_.size()) + " slot(s)";
  if (!bindingMessage.empty()) {
    outMessage += "; " + bindingMessage;
  }
  if (unavailableCount != 0u) {
    outMessage += "; " + std::to_string(unavailableCount) +
                  " authored binding override(s) use imported fallback";
  }
  if (selectedMaterialIndex_.has_value()) {
    std::string materialMessage{};
    if (SelectSlotForEditing(scene, *selectedMaterialIndex_, materialMessage) &&
        !materialMessage.empty()) {
      outMessage += "; " + materialMessage;
    }
  }
  return true;
}

void ModelMaterialWorkspaceController::RebuildSlotState(
    const AssetDatabase &assetDatabase, const AssetRegistry &assetRegistry) {
  previewScene_.ClearPreviewMaterialOverrides();
  slots_.clear();
  publicSlotViews_.clear();
  stats_ = {};

  const ModelAsset *model = previewScene_.GetModel();
  if (model == nullptr) {
    return;
  }
  stats_.nodeCount = static_cast<uint32_t>((std::min)(
      model->nodes.size(),
      static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
  stats_.meshCount = static_cast<uint32_t>((std::min)(
      model->meshes.size(),
      static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
  stats_.materialSlotCount = static_cast<uint32_t>((std::min)(
      model->materials.size(),
      static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));

  std::vector<uint32_t> surfaceCounts(model->materials.size(), 0u);
  for (const ModelNode &node : model->nodes) {
    if (node.meshIndex < 0 ||
        static_cast<size_t>(node.meshIndex) >= model->meshes.size()) {
      continue;
    }
    const MeshAsset &mesh = model->meshes[static_cast<size_t>(node.meshIndex)];
    for (const MeshPrimitive &primitive : mesh.primitives) {
      ++stats_.surfaceCount;
      stats_.vertexCount += primitive.staticVertices.size();
      stats_.vertexCount += primitive.skinnedVertices.size();
      stats_.triangleCount += primitive.indices.size() / 3u;
      if (primitive.materialIndex < surfaceCounts.size()) {
        ++surfaceCounts[primitive.materialIndex];
      } else {
        ++stats_.invalidSurfaceMaterialCount;
      }
    }
  }

  const auto &authoredBindings = authoringDocument_.GetBindings();
  slots_.reserve(model->materials.size());
  for (size_t index = 0; index < model->materials.size(); ++index) {
    const MaterialAsset &imported = model->materials[index];
    SlotState slot{};
    slot.view.materialIndex = static_cast<uint32_t>(index);
    slot.view.slotId = imported.slotId;
    slot.view.displayName = imported.name.empty()
                                ? "Material " + std::to_string(index)
                                : imported.name;
    slot.view.surfaceCount = surfaceCounts[index];
    if (!imported.slotId.IsValid()) {
      ++stats_.invalidSlotIdCount;
      slot.view.diagnostic =
          "stable slotId is missing; reimport before authoring a binding";
    } else {
      const auto overrideFound = authoredBindings.find(imported.slotId.value);
      if (overrideFound != authoredBindings.end()) {
        slot.view.overrideMaterialGuid = overrideFound->second;
      }
    }
    slots_.push_back(std::move(slot));
  }

  for (SlotState &slot : slots_) {
    if (!slot.view.overrideMaterialGuid.IsValid()) {
      continue;
    }
    std::string message{};
    (void)RebuildPreviewOverride(slot, assetDatabase, assetRegistry, message);
  }
  UpdatePublicSlotViews();
}

ModelMaterialWorkspaceController::SlotState *
ModelMaterialWorkspaceController::FindSlot(MaterialSlotId slotId) noexcept {
  const auto found = std::find_if(
      slots_.begin(), slots_.end(), [slotId](const SlotState &slot) {
        return slot.view.slotId == slotId;
      });
  return found != slots_.end() ? &*found : nullptr;
}

const ModelMaterialWorkspaceController::SlotState *
ModelMaterialWorkspaceController::FindSlot(MaterialSlotId slotId) const noexcept {
  const auto found = std::find_if(
      slots_.begin(), slots_.end(), [slotId](const SlotState &slot) {
        return slot.view.slotId == slotId;
      });
  return found != slots_.end() ? &*found : nullptr;
}

ModelMaterialWorkspaceController::SlotState *
ModelMaterialWorkspaceController::FindSlotByMaterialIndex(
    uint32_t materialIndex) noexcept {
  const auto found = std::find_if(
      slots_.begin(), slots_.end(), [materialIndex](const SlotState &slot) {
        return slot.view.materialIndex == materialIndex;
      });
  return found != slots_.end() ? &*found : nullptr;
}

const ModelMaterialWorkspaceController::SlotState *
ModelMaterialWorkspaceController::FindSlotByMaterialIndex(
    uint32_t materialIndex) const noexcept {
  const auto found = std::find_if(
      slots_.begin(), slots_.end(), [materialIndex](const SlotState &slot) {
        return slot.view.materialIndex == materialIndex;
      });
  return found != slots_.end() ? &*found : nullptr;
}

void ModelMaterialWorkspaceController::UpdatePublicSlotViews() {
  publicSlotViews_.clear();
  publicSlotViews_.reserve(slots_.size());
  for (const SlotState &slot : slots_) {
    publicSlotViews_.push_back(slot.view);
  }
}

} // namespace HIKARI::EDITOR
