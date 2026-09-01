#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Assets/Material/Authoring/HIKARI_MaterialAssetAuthoringService.h"
#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Assets/Models/MaterialBindings/HIKARI_ModelMaterialBindingAsset.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

void ModelMaterialWorkspaceController::EnsureBuiltInRampPresets(
    DocumentSceneBase &scene) {
  AssetDatabase &assetDatabase = scene.GetAssetDatabase();
  const std::filesystem::path projectRoot =
      assetDatabase.GetProjectRoot().lexically_normal();
  if (rampPresetProjectRoot_ == projectRoot) {
    return;
  }

  ASSETS::MATERIAL::MaterialAssetAuthoringService service(assetDatabase);
  bool createdAny = false;
  for (const ASSETS::MATERIAL::MaterialRampPresetInfo &preset :
       ASSETS::MATERIAL::GetBuiltInMaterialRampPresets()) {
    const std::filesystem::path destination =
        assetDatabase.GetAssetsRoot() / "Materials" / "Ramps" /
        (std::string(preset.fileStem) + ".material.json");
    if (std::filesystem::exists(destination)) {
      continue;
    }
    const ASSETS::MATERIAL::MaterialAuthoringResult created = service.SaveAs(
        ASSETS::MATERIAL::CreateMaterialRampFromPreset(
            preset.id, preset.displayName),
        destination);
    if (!created.success) {
      statusMessage_ = "Could not create built-in Ramp preset '" +
                       std::string(preset.displayName) + "': " +
                       created.message;
      return;
    }
    createdAny = true;
  }
  if (createdAny) {
    if (!scene.ReloadAssets()) {
      statusMessage_ =
          "Built-in Ramp assets were created, but Registry publication "
          "failed";
      return;
    }
    assetChoiceRevision_ = 0u;
  }
  rampPresetProjectRoot_ = projectRoot;
  RefreshAssetChoices(assetDatabase);
}

bool ModelMaterialWorkspaceController::BindBuiltInRampPresets(
    MaterialAssetData &source) const {
  if (source.kind != MaterialAssetKind::Material) {
    return false;
  }
  const auto findPreset = [this](
                              ASSETS::MATERIAL::MaterialRampPresetId id)
      -> const AssetChoice * {
    const auto &presets = ASSETS::MATERIAL::GetBuiltInMaterialRampPresets();
    const auto info = std::find_if(
        presets.begin(), presets.end(), [id](const auto &preset) {
          return preset.id == id;
        });
    if (info == presets.end()) {
      return nullptr;
    }
    const std::string filename =
        std::string(info->fileStem) + ".material.json";
    const auto choice = std::find_if(
        materialChoices_.begin(), materialChoices_.end(),
        [&filename](const AssetChoice &candidate) {
          return candidate.ready &&
                 candidate.materialKind == MaterialAssetKind::Ramp &&
                 candidate.sourcePath.filename().string() == filename;
        });
    return choice != materialChoices_.end() ? &*choice : nullptr;
  };

  size_t rampIndex = 0u;
  bool changed = false;
  for (MaterialGraphResourceData &resource : source.graph.resources) {
    if (resource.semantic != MaterialResourceSemantic::Ramp &&
        resource.type != MaterialResourceType::Ramp1D &&
        resource.type != MaterialResourceType::Ramp2D) {
      continue;
    }
    if (resource.assetGuid.IsValid()) {
      ++rampIndex;
      continue;
    }
    const auto presetId = rampIndex == 0u
        ? ASSETS::MATERIAL::MaterialRampPresetId::ToonSoft
        : ASSETS::MATERIAL::MaterialRampPresetId::CoolShadow;
    if (const AssetChoice *preset = findPreset(presetId)) {
      resource.assetGuid = preset->guid;
      resource.enabled = true;
      changed = true;
    }
    ++rampIndex;
  }
  return changed;
}

void ModelMaterialWorkspaceController::RefreshAssetChoices(
    const AssetDatabase &assetDatabase) {
  const uint64_t revision = assetDatabase.GetContentRevision();
  if (assetChoiceRevision_ == revision) {
    return;
  }
  assetChoiceRevision_ = revision;
  modelChoices_.clear();
  materialChoices_.clear();
  materialSetChoices_.clear();
  textureChoices_.clear();

  const auto collect = [&assetDatabase](AssetType type,
                                        std::vector<AssetChoice> &out) {
    for (const AssetRecord *record : assetDatabase.CollectByType(type)) {
      if (record == nullptr || !record->guid.IsValid()) {
        continue;
      }
      AssetChoice choice{};
      choice.guid = record->guid;
      choice.label = GetAssetRecordDisplayName(*record);
      choice.sourcePath = record->sourcePath;
      choice.ready = record->lastImportSucceeded && !record->artifactMissing;
      out.push_back(std::move(choice));
    }
    std::sort(out.begin(), out.end(),
              [](const AssetChoice &left, const AssetChoice &right) {
                if (left.label != right.label) {
                  return left.label < right.label;
                }
                return left.guid.value < right.guid.value;
              });
  };
  collect(AssetType::Model, modelChoices_);
  collect(AssetType::Texture, textureChoices_);
  for (const AssetRecord *record :
       assetDatabase.CollectByType(AssetType::ModelMaterialBinding)) {
    if (record == nullptr || !record->guid.IsValid()) {
      continue;
    }
    ASSETS::MODELS::ModelMaterialBindingAssetData data{};
    std::string error{};
    const std::filesystem::path sourcePath = record->sourcePath.is_absolute()
        ? record->sourcePath
        : assetDatabase.GetProjectRoot() / record->sourcePath;
    if (!ASSETS::MODELS::LoadModelMaterialBindingAsset(
            sourcePath.lexically_normal(), data, error)) {
      continue;
    }
    AssetChoice choice{};
    choice.guid = record->guid;
    choice.label = data.displayName.empty()
        ? GetAssetRecordDisplayName(*record)
        : data.displayName;
    choice.sourcePath = record->sourcePath;
    choice.ready = record->lastImportSucceeded && !record->artifactMissing;
    choice.ownerModelGuid = data.modelAssetGuid;
    materialSetChoices_.push_back(std::move(choice));
  }
  std::sort(materialSetChoices_.begin(), materialSetChoices_.end(),
            [](const AssetChoice &left, const AssetChoice &right) {
              if (left.label != right.label) {
                return left.label < right.label;
              }
              return left.guid.value < right.guid.value;
            });
  for (AssetChoice &choice : textureChoices_) {
    choice.textureAsset = true;
  }
  for (const AssetRecord *record :
       assetDatabase.CollectByType(AssetType::Material)) {
    if (record == nullptr || !record->guid.IsValid()) {
      continue;
    }
    AssetChoice choice{};
    choice.guid = record->guid;
    choice.label = GetAssetRecordDisplayName(*record);
    choice.sourcePath = record->sourcePath;
    choice.ready = record->lastImportSucceeded && !record->artifactMissing;
    MaterialAssetData source{};
    std::string error{};
    const std::filesystem::path sourcePath = record->sourcePath.is_absolute()
        ? record->sourcePath
        : assetDatabase.GetProjectRoot() / record->sourcePath;
    if (LoadMaterialAssetData(sourcePath.lexically_normal(), source, error)) {
      choice.materialKind = source.kind;
      if (source.kind == MaterialAssetKind::Ramp) {
        choice.rampPreview = source.ramp;
      } else if (source.kind == MaterialAssetKind::Function) {
        choice.functionInputs = source.function.inputs;
        choice.functionOutputs = source.function.outputs;
      }
    } else {
      choice.ready = false;
    }
    materialChoices_.push_back(std::move(choice));
  }
  std::sort(materialChoices_.begin(), materialChoices_.end(),
            [](const AssetChoice &left, const AssetChoice &right) {
              if (left.materialKind != right.materialKind) {
                return static_cast<uint8_t>(left.materialKind) <
                       static_cast<uint8_t>(right.materialKind);
              }
              if (left.label != right.label) {
                return left.label < right.label;
              }
              return left.guid.value < right.guid.value;
            });
}

const ModelMaterialWorkspaceController::AssetChoice *
ModelMaterialWorkspaceController::FindMaterialChoice(
    const AssetGuid &guid) const noexcept {
  const auto found = std::find_if(
      materialChoices_.begin(), materialChoices_.end(),
      [&guid](const AssetChoice &choice) { return choice.guid == guid; });
  return found != materialChoices_.end() ? &*found : nullptr;
}

const ModelMaterialWorkspaceController::AssetChoice *
ModelMaterialWorkspaceController::FindResourceChoice(
    const AssetGuid &guid) const noexcept {
  if (const AssetChoice *material = FindMaterialChoice(guid)) {
    return material;
  }
  const auto found = std::find_if(
      textureChoices_.begin(), textureChoices_.end(),
      [&guid](const AssetChoice &choice) { return choice.guid == guid; });
  return found != textureChoices_.end() ? &*found : nullptr;
}

} // namespace HIKARI::EDITOR
