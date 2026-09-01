#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <filesystem>
#include <string>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Models/MaterialBindings/HIKARI_ModelMaterialBindingAuthoringService.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {
namespace {

ASSETS::MODELS::ModelMaterialBindingAssetData BuildBindingAssetData(
    const ModelMaterialWorkspaceDocument &document,
    std::string_view displayName) {
  ASSETS::MODELS::ModelMaterialBindingAssetData result{};
  result.displayName = displayName.empty()
                           ? "Model Material Set"
                           : std::string(displayName) + " Material Set";
  result.modelAssetGuid = document.GetModelGuid();
  result.bindings.reserve(document.GetBindings().size());
  for (const auto &[slotId, materialGuid] : document.GetBindings()) {
    result.bindings.push_back({MaterialSlotId{slotId}, materialGuid});
  }
  return result;
}

ModelMaterialWorkspaceDocument::BindingMap ToBindingMap(
    const ASSETS::MODELS::ModelMaterialBindingAssetData &data) {
  ModelMaterialWorkspaceDocument::BindingMap result{};
  result.reserve(data.bindings.size());
  for (const ASSETS::MODELS::ModelMaterialBindingEntry &entry :
       data.bindings) {
    if (entry.slotId.IsValid() && entry.materialAssetGuid.IsValid()) {
      result[entry.slotId.value] = entry.materialAssetGuid;
    }
  }
  return result;
}

} // namespace

bool ModelMaterialWorkspaceController::LoadOrCreateBindings(
    DocumentSceneBase &scene, std::string &outMessage) {
  AssetDatabase &database = scene.GetAssetDatabase();
  ASSETS::MODELS::ModelMaterialBindingAuthoringService service(database);
  ASSETS::MODELS::ModelMaterialBindingAuthoringDocument selected{};
  size_t matchCount = 0u;
  for (const AssetRecord *record :
       database.CollectByType(AssetType::ModelMaterialBinding)) {
    if (record == nullptr || !record->guid.IsValid()) {
      continue;
    }
    ASSETS::MODELS::ModelMaterialBindingAuthoringDocument candidate{};
    std::string loadMessage{};
    if (!service.Load(record->guid, candidate, loadMessage) ||
        candidate.data.modelAssetGuid != modelGuid_) {
      continue;
    }
    ++matchCount;
    if (!selected.assetGuid.IsValid()) {
      selected = std::move(candidate);
    }
  }

  if (selected.assetGuid.IsValid()) {
    authoringDocument_.OpenBindings(
        modelGuid_, selected.assetGuid, selected.sourcePath,
        ToBindingMap(selected.data), true);
    outMessage = matchCount > 1u
                     ? "opened the first compatible Material Set; choose another from Character Parts if needed"
                     : "opened the model Material Set";
    return true;
  }

  const ASSETS::MODELS::ModelMaterialBindingAuthoringResult created =
      service.Create(modelGuid_, modelDisplayName_ + " Material Set");
  if (!created.success) {
    authoringDocument_.OpenBindings(modelGuid_, {}, {}, {}, false);
    outMessage = created.message.empty()
                     ? "Material Set could not be created; an unsaved set remains editable"
                     : created.message + "; an unsaved set remains editable";
    return false;
  }
  if (!scene.ReloadAssets()) {
    outMessage =
        "Material Set was created, but Registry publication failed";
    return false;
  }
  authoringDocument_.OpenBindings(modelGuid_, created.assetGuid,
                                  created.sourcePath, {}, true);
  outMessage = "created a model-owned Material Set";
  return true;
}

bool ModelMaterialWorkspaceController::OpenMaterialSet(
    DocumentSceneBase &scene, const AssetGuid &guid,
    std::string &outMessage) {
  if (!executingPendingTransition_ &&
      authoringDocument_.IsBindingDirty()) {
    PendingTransition transition{};
    transition.kind = PendingTransitionKind::OpenMaterialSet;
    transition.assetGuid = guid;
    QueuePendingTransition(std::move(transition), outMessage);
    return false;
  }
  if (!modelGuid_.IsValid() || !guid.IsValid()) {
    outMessage = "select a model and Material Set first";
    return false;
  }
  ASSETS::MODELS::ModelMaterialBindingAuthoringService service(
      scene.GetAssetDatabase());
  ASSETS::MODELS::ModelMaterialBindingAuthoringDocument selected{};
  if (!service.Load(guid, selected, outMessage)) {
    return false;
  }
  if (selected.data.modelAssetGuid != modelGuid_) {
    outMessage = "Material Set belongs to a different model and was rejected";
    return false;
  }
  authoringDocument_.OpenBindings(
      modelGuid_, selected.assetGuid, selected.sourcePath,
      ToBindingMap(selected.data), true);
  RebuildSlotState(scene.GetAssetDatabase(), scene.GetAssetRegistry());
  outMessage = "opened Material Set " + selected.data.displayName;
  return true;
}

bool ModelMaterialWorkspaceController::CreateMaterialSet(
    DocumentSceneBase &scene, std::string &outMessage) {
  if (!executingPendingTransition_ &&
      authoringDocument_.IsBindingDirty()) {
    PendingTransition transition{};
    transition.kind = PendingTransitionKind::CreateMaterialSet;
    QueuePendingTransition(std::move(transition), outMessage);
    return false;
  }
  if (!modelGuid_.IsValid()) {
    outMessage = "select a model before creating a Material Set";
    return false;
  }
  ASSETS::MODELS::ModelMaterialBindingAuthoringService service(
      scene.GetAssetDatabase());
  const auto created = service.Create(
      modelGuid_, modelDisplayName_ + " Material Set");
  outMessage = created.message;
  if (!created.success) {
    return false;
  }
  if (!scene.ReloadAssets()) {
    outMessage += "; editor registry refresh failed";
    return false;
  }
  authoringDocument_.OpenBindings(
      modelGuid_, created.assetGuid, created.sourcePath, {}, true);
  assetChoiceRevision_ = 0u;
  RebuildSlotState(scene.GetAssetDatabase(), scene.GetAssetRegistry());
  return true;
}

bool ModelMaterialWorkspaceController::SaveBindings(
    DocumentSceneBase &scene, bool saveAs, std::string &outMessage) {
  if (!authoringDocument_.HasBindings() ||
      !authoringDocument_.GetModelGuid().IsValid()) {
    outMessage = "open a model before saving its Material Set";
    return false;
  }

  AssetDatabase &database = scene.GetAssetDatabase();
  ASSETS::MODELS::ModelMaterialBindingAuthoringService service(database);
  ASSETS::MODELS::ModelMaterialBindingAssetData source =
      BuildBindingAssetData(authoringDocument_, modelDisplayName_);
  ASSETS::MODELS::ModelMaterialBindingAuthoringResult saved{};
  if (!saveAs && authoringDocument_.GetBindingAssetGuid().IsValid()) {
    if (!authoringDocument_.GetBindingSourcePath().empty()) {
      source.displayName =
          authoringDocument_.GetBindingSourcePath().stem().string();
    }
    saved = service.Save(authoringDocument_.GetBindingAssetGuid(), source);
  } else {
    std::filesystem::path destination(bindingSaveAsPath_.data());
    if (destination.empty()) {
      destination = std::filesystem::path("Materials") /
                    (modelDisplayName_ + "_MaterialSet");
    }
    source.displayName = destination.stem().string();
    saved = service.SaveAs(source, destination);
  }
  outMessage = saved.message;
  if (!saved.success) {
    return false;
  }
  if (!scene.ReloadAssets()) {
    outMessage += "; editor registry refresh failed";
    return false;
  }
  authoringDocument_.MarkBindingsSaved(saved.assetGuid, saved.sourcePath);
  (void)scene.RebuildMaterialOverrides();
  return true;
}

} // namespace HIKARI::EDITOR
