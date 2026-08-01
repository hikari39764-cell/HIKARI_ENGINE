#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <filesystem>
#include <string>

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
                           ? "Model Material Bindings"
                           : std::string(displayName) + " Materials";
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
                     ? "loaded the first matching binding asset; duplicates need review"
                     : "loaded model material bindings";
    return true;
  }

  const ASSETS::MODELS::ModelMaterialBindingAuthoringResult created =
      service.Create(modelGuid_, modelDisplayName_ + " Materials");
  if (!created.success) {
    authoringDocument_.OpenBindings(modelGuid_, {}, {}, {}, false);
    outMessage = created.message.empty()
                     ? "binding asset could not be created; unsaved document remains editable"
                     : created.message + "; unsaved document remains editable";
    return false;
  }
  authoringDocument_.OpenBindings(modelGuid_, created.assetGuid,
                                  created.sourcePath, {}, true);
  (void)scene.ReloadAssets();
  outMessage = "created the model's default material binding asset";
  return true;
}

bool ModelMaterialWorkspaceController::SaveBindings(
    DocumentSceneBase &scene, bool saveAs, std::string &outMessage) {
  if (!authoringDocument_.HasBindings() ||
      !authoringDocument_.GetModelGuid().IsValid()) {
    outMessage = "open a model before saving material bindings";
    return false;
  }

  AssetDatabase &database = scene.GetAssetDatabase();
  ASSETS::MODELS::ModelMaterialBindingAuthoringService service(database);
  const ASSETS::MODELS::ModelMaterialBindingAssetData source =
      BuildBindingAssetData(authoringDocument_, modelDisplayName_);
  ASSETS::MODELS::ModelMaterialBindingAuthoringResult saved{};
  if (!saveAs && authoringDocument_.GetBindingAssetGuid().IsValid()) {
    saved = service.Save(authoringDocument_.GetBindingAssetGuid(), source);
  } else {
    std::filesystem::path destination(bindingSaveAsPath_.data());
    if (destination.empty()) {
      destination = std::filesystem::path("Materials") /
                    (modelDisplayName_ + "_Bindings");
    }
    saved = service.SaveAs(source, destination);
  }
  outMessage = saved.message;
  if (!saved.success) {
    return false;
  }
  authoringDocument_.MarkBindingsSaved(saved.assetGuid, saved.sourcePath);
  if (!scene.ReloadAssets()) {
    outMessage += "; editor registry refresh failed";
  } else {
    (void)scene.RebuildMaterialOverrides();
  }
  return true;
}

} // namespace HIKARI::EDITOR
