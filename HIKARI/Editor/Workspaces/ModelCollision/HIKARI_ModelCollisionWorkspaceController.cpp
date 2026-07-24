#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionWorkspaceController.h"

#include <algorithm>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionWorkspaceInteraction.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

void ModelCollisionWorkspaceController::ApplyWorkspaceActivation(
    DocumentSceneBase &scene, const EditorWorkspaceActivation &activation,
    EditorWorkspaceHost &workspaceHost) {

  if (activation.previous == EditorWorkspaceId::ModelCollision &&
      activation.current != EditorWorkspaceId::ModelCollision) {
    if (assetTaskService_ != nullptr && generationTaskId_ != 0u) {
      (void)assetTaskService_->RequestCancel(generationTaskId_);
    }
    ClosePreviewRequest(workspaceHost);
    return;
  }
  if (activation.current != EditorWorkspaceId::ModelCollision ||
      !activation.modelAssetGuid.has_value()) {
    return;
  }

  const AssetGuid &requested = *activation.modelAssetGuid;
  if (history_.IsDirty() && modelGuid_.IsValid() &&
      modelGuid_.value != requested.value) {
    pendingModelGuid_ = requested;
    return;
  }
  if (!OpenModel(scene.GetAssetDatabase(), requested, statusMessage_)) {
    pendingModelGuid_ = {};
  }
}

bool ModelCollisionWorkspaceController::IsEditingModel() const noexcept {
  return modelGuid_.IsValid() && previewScene_.IsReady();
}

bool ModelCollisionWorkspaceController::IsDocumentDirty() const noexcept {
  return history_.IsDirty();
}

bool ModelCollisionWorkspaceController::CanUndo() const noexcept {
  return history_.CanUndo();
}

bool ModelCollisionWorkspaceController::CanRedo() const noexcept {
  return history_.CanRedo();
}

void ModelCollisionWorkspaceController::BindSelectionCommands(
    EditorCommandRouter &commandRouter) {

  const bool shapeSelection =
      selectionMode_ == ModelCollisionSelectionMode::CollisionShapes &&
      !selectedShapeIds_.empty();
  const bool sourceSelection =
      selectionMode_ == ModelCollisionSelectionMode::SourceNodes &&
      !selectedSourceNodes_.empty();
  commandRouter.Bind(EditorCommandId::DuplicateSelection,
                     "Duplicate Collision Shape", shapeSelection,
                     [this]() { DuplicateSelectedShapes(); });
  commandRouter.Bind(EditorCommandId::DeleteSelection, "Delete Collision Shape",
                     shapeSelection, [this]() { DeleteSelectedShapes(); });
  commandRouter.Bind(
      EditorCommandId::FocusSelection, "Focus Collision Selection",
      shapeSelection || sourceSelection, [this]() { FocusSelection(); });
}

bool ModelCollisionWorkspaceController::SaveDocument(DocumentSceneBase &scene,
                                                     std::string &outMessage) {

  if (!modelGuid_.IsValid() || setupPath_.empty()) {
    outMessage = "no model collision document is open";
    return false;
  }
  if (!ASSETS::COLLISION::SaveModelCollisionSetup(setupPath_, setup_,
                                                  outMessage)) {
    return false;
  }
  std::string artifactMessage{};
  if (!scene.GetAssetDatabase().RebuildModelCollisionArtifact(
          modelGuid_, artifactMessage)) {
    outMessage =
        "setup saved, but runtime collision build failed: " + artifactMessage;
    return false;
  }
  history_.MarkSaved();
  const bool registryRefreshed = scene.ReloadAssets();
  outMessage = setup_.shapes.empty()
                   ? "collision setup saved; runtime collision removed"
                   : "collision setup and runtime artifact saved";
  if (!artifactMessage.empty()) {
    outMessage += " (" + artifactMessage + ")";
  }
  if (!registryRefreshed) {
    outMessage += " (editor registry refresh failed; use Refresh Assets)";
  }
  return true;
}

bool ModelCollisionWorkspaceController::Undo(std::string &outMessage) {

  if (!history_.Undo(setup_)) {
    outMessage = "nothing to undo";
    return false;
  }
  generationDraft_.reset();
  NormalizeShapeSelection();
  ++editRevision_;
  outMessage = "collision edit undone";
  return true;
}

bool ModelCollisionWorkspaceController::Redo(std::string &outMessage) {

  if (!history_.Redo(setup_)) {
    outMessage = "nothing to redo";
    return false;
  }
  generationDraft_.reset();
  NormalizeShapeSelection();
  ++editRevision_;
  outMessage = "collision edit redone";
  return true;
}

void ModelCollisionWorkspaceController::BindDocumentCommands(
    EditorCommandRouter &commandRouter, DocumentSceneBase &scene) {

  commandRouter.Bind(
      EditorCommandId::SaveDocument, "Save Model Collision", IsEditingModel(),
      [this, &scene]() { (void)SaveDocument(scene, statusMessage_); });
  commandRouter.Bind(EditorCommandId::Undo, "Undo Model Collision Edit",
                     CanUndo(), [this]() { (void)Undo(statusMessage_); });
  commandRouter.Bind(EditorCommandId::Redo, "Redo Model Collision Edit",
                     CanRedo(), [this]() { (void)Redo(statusMessage_); });
}

bool ModelCollisionWorkspaceController::OpenModel(AssetDatabase &assetDatabase,
                                                  const AssetGuid &guid,
                                                  std::string &outMessage) {

  const AssetRecord *record = assetDatabase.FindByGuid(guid);
  if (record == nullptr || record->type != AssetType::Model) {
    outMessage = "selected model is no longer available";
    return false;
  }
  if (!previewScene_.Load(*record, assetDatabase.GetProjectRoot(),
                          outMessage)) {
    return false;
  }
  sourceOutlineCache_.Reset();

  ASSETS::COLLISION::ModelCollisionSetup loaded{};
  const std::filesystem::path setupPath =
      ASSETS::COLLISION::GetModelCollisionSetupPath(*record);
  const ASSETS::COLLISION::ModelCollisionSetupLoadResult load =
      ASSETS::COLLISION::LoadModelCollisionSetup(setupPath, guid.value, loaded);
  bool legacySetupReplaced = false;
  if (!load.success) {
    if (loaded.version > 0u &&
        loaded.version != ASSETS::COLLISION::kModelCollisionSetupVersion) {
      loaded = {};
      loaded.modelAssetGuid = guid.value;
      legacySetupReplaced = true;
    } else {
      previewScene_.Clear();
      outMessage = load.message;
      return false;
    }
  }

  modelGuid_ = guid;
  if (assetTaskService_ != nullptr && generationTaskId_ != 0u) {
    (void)assetTaskService_->RequestCancel(generationTaskId_);
  }
  generationTaskId_ = 0u;
  generationTaskOutput_.reset();
  generationPending_ = false;
  generationDraft_.reset();
  pendingModelGuid_ = {};
  modelDisplayName_ = record->displayName;
  setupPath_ = setupPath;
  setup_ = std::move(loaded);
  ++editRevision_;
  history_.Reset(setup_);
  selectedSourceNodes_.clear();
  selectedShapeIds_.clear();
  hiddenShapeIds_.clear();
  lockedShapeIds_.clear();
  primarySourceNodeIndex_ = -1;
  hoveredSourceNodeIndex_ = -1;
  hoveredShapeId_ = 0u;
  selectedShapeId_ = 0u;
  SelectFirstShape();
  FitPreviewCamera();
  outMessage = legacySetupReplaced ? "legacy collision setup was intentionally "
                                     "discarded; save to replace it"
               : load.exists       ? "collision setup opened"
                                   : "new collision setup opened";
  return true;
}

void ModelCollisionWorkspaceController::ClosePreviewRequest(
    EditorWorkspaceHost &workspaceHost) const {

#if defined(HIKARI_WITH_EDITOR)
  RENDER3D::EDITORVIEW::ClearRequest(
      workspaceHost.GetModelCollisionPreviewView().renderViewId);
#else
  (void)workspaceHost;
#endif
}

void ModelCollisionWorkspaceController::FitPreviewCamera() {
  const ModelAsset *model = previewScene_.GetModel();
  if (model == nullptr) {
    return;
  }
  const Bounds bounds = BOUNDS::IsUsable(model->bounds)
                            ? model->bounds
                            : BOUNDS::ComputeModelBounds(*model);
  if (!BOUNDS::IsUsable(bounds)) {
    return;
  }
  const MATH::Vec3 center = (bounds.min + bounds.max) * 0.5f;
  const float radius =
      (std::max)(MATH::Length(bounds.max - bounds.min) * 0.5f, 0.5f);
  EditorDirectorCameraSettings &navigation = cameraController_.Settings();
  navigation.moveSpeed = std::clamp(radius * 0.75f, 5.0f, 1000.0f);
  navigation.fastMultiplier = 5.0f;
  navigation.wheelMoveStep = std::clamp(radius * 0.12f, 0.5f, 200.0f);
  cameraController_.ApplyViewPreset(
      EditorDirectorCameraViewPreset::Perspective);
  cameraController_.Focus(center, radius * 2.4f);
  Camera3D &camera = cameraController_.GetCamera();
  camera.SetPerspective(camera.GetFovYRad(), camera.GetAspect(),
                        (std::max)(0.001f, radius * 0.001f),
                        (std::max)(100.0f, radius * 20.0f));
  ++cameraRevision_;
  cameraCutPending_ = true;
}

void ModelCollisionWorkspaceController::FocusPreviewBounds(
    const Bounds &bounds) {

  if (!BOUNDS::IsUsable(bounds)) {
    return;
  }
  const MATH::Vec3 center = (bounds.min + bounds.max) * 0.5f;
  const float radius =
      (std::max)(MATH::Length(bounds.max - bounds.min) * 0.5f, 0.05f);
  EditorDirectorCameraSettings &navigation = cameraController_.Settings();
  navigation.moveSpeed = std::clamp(radius * 0.75f, 0.05f, 1000.0f);
  navigation.wheelMoveStep = std::clamp(radius * 0.12f, 0.01f, 200.0f);
  cameraController_.Focus(center, radius * 2.4f);
  ++cameraRevision_;
  cameraCutPending_ = true;
}

void ModelCollisionWorkspaceController::FocusSelection() {
  Bounds focusBounds = BOUNDS::EmptyBounds();
  bool hasBounds = false;
  if (selectionMode_ == ModelCollisionSelectionMode::SourceNodes &&
      !selectedSourceNodes_.empty()) {
    for (int32_t nodeIndex : selectedSourceNodes_) {
      const ModelCollisionPreviewNode *node =
          previewScene_.FindSourceNode(nodeIndex);
      if (node == nullptr) {
        continue;
      }
      BOUNDS::Encapsulate(focusBounds, node->bounds);
      hasBounds = true;
    }
  } else {
    for (const ASSETS::COLLISION::ModelCollisionShape &shape : setup_.shapes) {
      if (!selectedShapeIds_.contains(shape.id)) {
        continue;
      }
      BOUNDS::Encapsulate(focusBounds, ComputeModelCollisionShapeBounds(shape));
      hasBounds = true;
    }
  }
  if (!hasBounds) {
    FitPreviewCamera();
    return;
  }
  FocusPreviewBounds(focusBounds);
}

void ModelCollisionWorkspaceController::CommitEdit(std::string label) {
  generationDraft_.reset();
  history_.Commit(setup_, std::move(label));
  ++editRevision_;
}

void ModelCollisionWorkspaceController::NormalizeShapeSelection() noexcept {
  std::erase_if(selectedShapeIds_, [this](uint64_t shapeId) {
    return setup_.FindShape(shapeId) == nullptr;
  });
  std::erase_if(hiddenShapeIds_, [this](uint64_t shapeId) {
    return setup_.FindShape(shapeId) == nullptr;
  });
  std::erase_if(lockedShapeIds_, [this](uint64_t shapeId) {
    return setup_.FindShape(shapeId) == nullptr;
  });
  if (setup_.FindShape(selectedShapeId_) != nullptr &&
      selectedShapeIds_.contains(selectedShapeId_)) {
    return;
  }
  selectedShapeId_ = 0u;
  for (const ASSETS::COLLISION::ModelCollisionShape &shape : setup_.shapes) {
    if (selectedShapeIds_.contains(shape.id)) {
      selectedShapeId_ = shape.id;
      return;
    }
  }
}

void ModelCollisionWorkspaceController::SelectFirstShape() noexcept {
  selectedShapeIds_.clear();
  selectedShapeId_ = 0u;
  if (!setup_.shapes.empty()) {
    selectedShapeId_ = setup_.shapes.front().id;
    selectedShapeIds_.insert(selectedShapeId_);
  }
}

void ModelCollisionWorkspaceController::SelectShape(uint64_t shapeId,
                                                    bool additive) noexcept {

  if (shapeId == 0u || setup_.FindShape(shapeId) == nullptr) {
    if (!additive) {
      selectedShapeIds_.clear();
      selectedShapeId_ = 0u;
    }
    return;
  }
  if (!additive) {
    selectedShapeIds_.clear();
    selectedShapeIds_.insert(shapeId);
    selectedShapeId_ = shapeId;
    return;
  }
  if (selectedShapeIds_.contains(shapeId)) {
    selectedShapeIds_.erase(shapeId);
    if (selectedShapeId_ == shapeId) {
      selectedShapeId_ = 0u;
    }
  } else {
    selectedShapeIds_.insert(shapeId);
    selectedShapeId_ = shapeId;
  }
  NormalizeShapeSelection();
}

void ModelCollisionWorkspaceController::SelectSourceNode(
    int32_t nodeIndex, bool additive) noexcept {

  if (nodeIndex < 0 || previewScene_.FindSourceNode(nodeIndex) == nullptr) {
    if (!additive) {
      selectedSourceNodes_.clear();
      primarySourceNodeIndex_ = -1;
    }
    return;
  }
  if (!additive) {
    selectedSourceNodes_.clear();
    selectedSourceNodes_.insert(nodeIndex);
    primarySourceNodeIndex_ = nodeIndex;
  } else if (selectedSourceNodes_.contains(nodeIndex)) {
    selectedSourceNodes_.erase(nodeIndex);
    if (primarySourceNodeIndex_ == nodeIndex) {
      primarySourceNodeIndex_ =
          selectedSourceNodes_.empty() ? -1 : *selectedSourceNodes_.begin();
    }
  } else {
    selectedSourceNodes_.insert(nodeIndex);
    primarySourceNodeIndex_ = nodeIndex;
  }
  sourceNodeScrollRequest_ = nodeIndex;
}

bool ModelCollisionWorkspaceController::IsShapeSelected(
    uint64_t shapeId) const noexcept {
  return selectedShapeIds_.contains(shapeId);
}

bool ModelCollisionWorkspaceController::IsShapeHidden(
    uint64_t shapeId) const noexcept {
  return hiddenShapeIds_.contains(shapeId);
}

bool ModelCollisionWorkspaceController::IsShapeLocked(
    uint64_t shapeId) const noexcept {
  return lockedShapeIds_.contains(shapeId);
}

bool ModelCollisionWorkspaceController::AreAllSelectedShapesHidden()
    const noexcept {

  return !selectedShapeIds_.empty() &&
         std::all_of(selectedShapeIds_.begin(), selectedShapeIds_.end(),
                     [this](uint64_t shapeId) {
                       return hiddenShapeIds_.contains(shapeId);
                     });
}

bool ModelCollisionWorkspaceController::AreAllSelectedShapesLocked()
    const noexcept {

  return !selectedShapeIds_.empty() &&
         std::all_of(selectedShapeIds_.begin(), selectedShapeIds_.end(),
                     [this](uint64_t shapeId) {
                       return lockedShapeIds_.contains(shapeId);
                     });
}

bool ModelCollisionWorkspaceController::ShouldDrawShape(
    uint64_t shapeId) const noexcept {
  if (IsShapeHidden(shapeId)) {
    return false;
  }
  switch (collisionVisibility_) {
  case ModelCollisionPreviewVisibility::All:
  case ModelCollisionPreviewVisibility::SelectedWithContext:
    return true;
  case ModelCollisionPreviewVisibility::SelectedOnly:
    return IsShapeSelected(shapeId) || shapeId == hoveredShapeId_;
  case ModelCollisionPreviewVisibility::Hidden:
    return false;
  }
  return false;
}

float ModelCollisionWorkspaceController::ShapeOverlayOpacity(
    uint64_t shapeId) const noexcept {
  if (collisionVisibility_ !=
          ModelCollisionPreviewVisibility::SelectedWithContext ||
      IsShapeSelected(shapeId) || shapeId == hoveredShapeId_) {
    return 1.0f;
  }
  return selectionMode_ == ModelCollisionSelectionMode::SourceNodes ? 0.12f
                                                                    : 0.28f;
}

void ModelCollisionWorkspaceController::ShowAllShapes() noexcept {
  hiddenShapeIds_.clear();
  collisionVisibility_ = ModelCollisionPreviewVisibility::All;
}

void ModelCollisionWorkspaceController::HideUnselectedShapes() noexcept {
  for (const ASSETS::COLLISION::ModelCollisionShape &shape : setup_.shapes) {
    if (!IsShapeSelected(shape.id)) {
      hiddenShapeIds_.insert(shape.id);
    }
  }
  collisionVisibility_ = ModelCollisionPreviewVisibility::SelectedOnly;
}

ASSETS::COLLISION::ModelCollisionShape *
ModelCollisionWorkspaceController::SelectedShape() noexcept {
  return setup_.FindShape(selectedShapeId_);
}

const ASSETS::COLLISION::ModelCollisionShape *
ModelCollisionWorkspaceController::SelectedShape() const noexcept {
  return setup_.FindShape(selectedShapeId_);
}

void ModelCollisionWorkspaceController::AddShape(
    ASSETS::COLLISION::CollisionGeometryShapeType type) {

  const ModelAsset *model = previewScene_.GetModel();
  if (model == nullptr) {
    return;
  }
  Bounds bounds = BOUNDS::IsUsable(model->bounds)
                      ? model->bounds
                      : BOUNDS::ComputeModelBounds(*model);
  bool fittedToSelection = false;
  if (selectionMode_ == ModelCollisionSelectionMode::SourceNodes &&
      !selectedSourceNodes_.empty()) {
    Bounds selectedBounds = BOUNDS::EmptyBounds();
    for (int32_t nodeIndex : selectedSourceNodes_) {
      const ModelCollisionPreviewNode *node =
          previewScene_.FindSourceNode(nodeIndex);
      if (node == nullptr) {
        continue;
      }
      BOUNDS::Encapsulate(selectedBounds, node->bounds);
      fittedToSelection = true;
    }
    if (fittedToSelection) {
      bounds = selectedBounds;
    }
  }
  if (!BOUNDS::IsUsable(bounds)) {
    return;
  }
  std::string name = "Box";
  if (type == ASSETS::COLLISION::CollisionGeometryShapeType::Sphere) {
    name = "Sphere";
  } else if (type == ASSETS::COLLISION::CollisionGeometryShapeType::Capsule) {
    name = "Capsule";
  }
  ASSETS::COLLISION::ModelCollisionShape shape =
      ASSETS::COLLISION::CreateFittedCollisionShape(
          setup_, type, bounds, fittedToSelection ? "Selection " + name : name,
          false);
  shape.name += " " + std::to_string(shape.id);
  selectedShapeIds_.clear();
  selectedShapeIds_.insert(shape.id);
  selectedShapeId_ = shape.id;
  setup_.shapes.push_back(std::move(shape));
  CommitEdit("Add Collision Shape");
}

void ModelCollisionWorkspaceController::DuplicateSelectedShapes() {
  if (selectedShapeIds_.empty()) {
    return;
  }
  std::vector<ASSETS::COLLISION::ModelCollisionShape> duplicates{};
  for (const ASSETS::COLLISION::ModelCollisionShape &shape : setup_.shapes) {
    if (!selectedShapeIds_.contains(shape.id)) {
      continue;
    }
    ASSETS::COLLISION::ModelCollisionShape duplicate = shape;
    duplicate.id = setup_.AllocateShapeId();
    duplicate.name += " Copy";
    duplicate.generated = false;
    duplicate.sourceNodeIndices.clear();
    duplicate.generationMethod.clear();
    duplicate.center.x += 0.1f;
    duplicates.push_back(std::move(duplicate));
  }
  if (duplicates.empty()) {
    return;
  }
  selectedShapeIds_.clear();
  for (ASSETS::COLLISION::ModelCollisionShape &duplicate : duplicates) {
    selectedShapeId_ = duplicate.id;
    selectedShapeIds_.insert(duplicate.id);
    setup_.shapes.push_back(std::move(duplicate));
  }
  CommitEdit(duplicates.size() == 1u ? "Duplicate Collision Shape"
                                     : "Duplicate Collision Shapes");
}

void ModelCollisionWorkspaceController::DeleteSelectedShapes() {
  if (selectedShapeIds_.empty()) {
    return;
  }
  const size_t before = setup_.shapes.size();
  std::erase_if(setup_.shapes,
                [this](const ASSETS::COLLISION::ModelCollisionShape &shape) {
                  return selectedShapeIds_.contains(shape.id) &&
                         !lockedShapeIds_.contains(shape.id);
                });
  if (setup_.shapes.size() == before) {
    statusMessage_ = "unlock selected collision shapes before deleting";
    return;
  }
  selectedShapeIds_.clear();
  SelectFirstShape();
  NormalizeShapeSelection();
  CommitEdit(before - setup_.shapes.size() == 1u ? "Delete Collision Shape"
                                                 : "Delete Collision Shapes");
}

void ModelCollisionWorkspaceController::SetSelectedShapesEnabled(bool enabled) {

  bool changed = false;
  for (ASSETS::COLLISION::ModelCollisionShape &shape : setup_.shapes) {
    if (selectedShapeIds_.contains(shape.id) && shape.enabled != enabled) {
      shape.enabled = enabled;
      changed = true;
    }
  }
  if (changed) {
    CommitEdit(enabled ? "Enable Collision Shapes"
                       : "Disable Collision Shapes");
  }
}

void ModelCollisionWorkspaceController::SetSelectedShapesHidden(
    bool hidden) noexcept {

  for (uint64_t shapeId : selectedShapeIds_) {
    if (hidden) {
      hiddenShapeIds_.insert(shapeId);
    } else {
      hiddenShapeIds_.erase(shapeId);
    }
  }
}

void ModelCollisionWorkspaceController::SetSelectedShapesLocked(
    bool locked) noexcept {

  for (uint64_t shapeId : selectedShapeIds_) {
    if (locked) {
      lockedShapeIds_.insert(shapeId);
    } else {
      lockedShapeIds_.erase(shapeId);
    }
  }
}

void ModelCollisionWorkspaceController::GenerateShapes() {
  if (generationPending_ || generationDraft_.has_value()) {
    return;
  }
  if (assetTaskService_ == nullptr) {
    statusMessage_ = "background asset task service is unavailable";
    return;
  }
  std::shared_ptr<const ModelAsset> model = previewScene_.GetSharedModel();
  if (!model) {
    return;
  }
  ASSETS::COLLISION::ModelCollisionGenerationRequest request{};
  request.target = generationTarget_;
  request.method = generationMethod_;
  request.replaceGeneratedShapes = replaceGeneratedShapes_;
  request.preserveGaps = generationPreserveGaps_;
  request.mergeDistance = generationMergeDistance_;
  request.accuracy = generationAccuracy_;
  request.maximumGeneratedShapes =
      static_cast<uint32_t>((std::max)(generationBudget_, 1));
  request.maximumHullVertices =
      static_cast<uint32_t>((std::clamp)(generationHullVertexBudget_, 16, 256));
  request.maximumTriangleCount =
      static_cast<uint32_t>((std::max)(generationTriangleBudget_, 1));
  request.sourceNodeIndices.assign(selectedSourceNodes_.begin(),
                                   selectedSourceNodes_.end());
  std::sort(request.sourceNodeIndices.begin(), request.sourceNodeIndices.end());
  generationStartRevision_ = editRevision_;
  generationTaskOutput_ = std::make_shared<CollisionGenerationTaskOutput>();
  ASSETS::COLLISION::ModelCollisionSetup setupSnapshot = setup_;
  const std::shared_ptr<CollisionGenerationTaskOutput> output =
      generationTaskOutput_;

  AssetTaskRequest taskRequest{};
  taskRequest.category = "Collision";
  taskRequest.label = "Generate collision for " + modelDisplayName_;
  taskRequest.initialItem = modelDisplayName_;
  taskRequest.cancelable = true;
  taskRequest.work =
      [model = std::move(model), request = std::move(request), output,
       setup = std::move(setupSnapshot)](AssetTaskContext &task) mutable {
        output->setup = std::move(setup);
        ASSETS::COLLISION::ModelCollisionGenerationControl control{
            [&task]() { return task.IsCancellationRequested(); },
            [&task](std::string stage, uint32_t completed, uint32_t total,
                    bool determinate) {
              AssetTaskProgress progress{};
              progress.stage = std::move(stage);
              progress.currentItem = "Collision source groups";
              progress.completedUnits = completed;
              progress.totalUnits = total;
              progress.determinate = determinate && total > 0u;
              progress.normalized = total > 0u ? static_cast<float>(completed) /
                                                     static_cast<float>(total)
                                               : 0.0f;
              task.Report(std::move(progress));
            }};
        output->result = ASSETS::COLLISION::GenerateModelCollisionShapes(
            *model, request, output->setup, &control);
        if (task.IsCancellationRequested() && !output->result.success) {
          return AssetTaskOutcome::Canceled(output->result.message);
        }
        return output->result.success
                   ? AssetTaskOutcome::Succeeded(output->result.message)
                   : AssetTaskOutcome::Failed(output->result.message);
      };
  generationTaskId_ = assetTaskService_->Submit(std::move(taskRequest));
  generationPending_ = generationTaskId_ != 0u;
  statusMessage_ = generationPending_ ? "generating collision in background..."
                                      : "failed to queue collision generation";
}

void ModelCollisionWorkspaceController::PollGenerationTask() {
  if (!generationPending_ || assetTaskService_ == nullptr ||
      generationTaskId_ == 0u) {
    return;
  }
  const std::optional<AssetTaskSnapshot> snapshot =
      assetTaskService_->FindSnapshot(generationTaskId_);
  if (!snapshot || !IsTerminal(snapshot->state)) {
    return;
  }

  generationPending_ = false;
  generationTaskId_ = 0u;
  if (snapshot->state == AssetTaskState::Canceled) {
    statusMessage_ = snapshot->resultMessage.empty()
                         ? "collision generation canceled"
                         : snapshot->resultMessage;
    generationTaskOutput_.reset();
    return;
  }
  if (snapshot->state == AssetTaskState::Failed || !generationTaskOutput_) {
    statusMessage_ = snapshot->resultMessage.empty()
                         ? "collision generation failed"
                         : snapshot->resultMessage;
    generationTaskOutput_.reset();
    return;
  }

  CollisionGenerationTaskOutput output = std::move(*generationTaskOutput_);
  generationTaskOutput_.reset();
  if (generationStartRevision_ != editRevision_) {
    statusMessage_ = "generation finished, but its result was discarded "
                     "because the document changed";
    return;
  }
  statusMessage_ = output.result.message;
  if (!output.result.success) {
    return;
  }
  generationDraft_ = std::move(output);
  statusMessage_ =
      "generation draft is ready; review the counts, then apply or discard it";
}

void ModelCollisionWorkspaceController::ApplyGenerationDraft() {
  if (!generationDraft_.has_value()) {
    return;
  }
  CollisionGenerationTaskOutput output = std::move(*generationDraft_);
  generationDraft_.reset();
  setup_ = std::move(output.setup);
  selectedShapeIds_.clear();
  for (uint64_t shapeId : output.result.generatedShapeIds) {
    selectedShapeIds_.insert(shapeId);
  }
  selectedShapeId_ = output.result.generatedShapeIds.empty()
                         ? 0u
                         : output.result.generatedShapeIds.front();
  NormalizeShapeSelection();
  CommitEdit("Generate Collision Shapes");
  statusMessage_ = output.result.message;
}

void ModelCollisionWorkspaceController::DiscardGenerationDraft() noexcept {
  generationDraft_.reset();
  statusMessage_ = "generation draft discarded";
}

} // namespace HIKARI::EDITOR
