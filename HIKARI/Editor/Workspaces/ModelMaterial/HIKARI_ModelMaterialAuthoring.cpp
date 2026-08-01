#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <filesystem>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/Material/Authoring/HIKARI_MaterialAssetAuthoringService.h"
#include "Assets/Material/Authoring/HIKARI_MaterialInstanceResolver.h"
#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Material/HIKARI_MaterialRuntimeBuilder.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {
namespace {

bool IsRenderableMaterialAsset(MaterialAssetKind kind) noexcept {
  return kind == MaterialAssetKind::Material ||
         kind == MaterialAssetKind::Instance;
}

} // namespace

bool ModelMaterialWorkspaceController::OpenMaterialAsset(
    DocumentSceneBase &scene, const AssetGuid &guid, std::string &outMessage) {
  if (!guid.IsValid()) {
    outMessage = "choose a valid material asset";
    return false;
  }
  if (authoringDocument_.HasMaterial() &&
      authoringDocument_.GetMaterialAssetGuid() == guid) {
    outMessage = "material is already open";
    return true;
  }
  if (authoringDocument_.IsMaterialDirty() ||
      authoringDocument_.IsAbPreviewActive()) {
    outMessage = "save, undo, or discard the current material changes first";
    return false;
  }

  ASSETS::MATERIAL::MaterialAssetAuthoringService service(
      scene.GetAssetDatabase());
  ASSETS::MATERIAL::MaterialAuthoringDocument loaded{};
  if (!service.Load(guid, loaded, outMessage)) {
    return false;
  }
  authoringDocument_.OpenMaterial(
      loaded.assetGuid, loaded.sourcePath, std::move(loaded.source),
      loaded.compile,
      loaded.hasLastGoodProgram ? &loaded.lastGoodProgram : nullptr, true);
  candidateAuthoringAssetGuid_ = guid;
  if (IsRenderableMaterialAsset(authoringDocument_.GetMaterial().kind)) {
    candidateMaterialGuid_ = guid;
  }
  selectedGraphNodeId_.clear();
  selectedGraphEdgeId_.clear();
  selectedRampPointId_.clear();
  materialGesture_.reset();
  instanceParentCache_.reset();
  instanceParentCacheGuid_ = {};
  instanceParentCacheRevision_ = 0u;
  if (IsRenderableMaterialAsset(authoringDocument_.GetMaterial().kind)) {
    (void)RebuildAuthoredPreview(scene.GetAssetDatabase(),
                                 scene.GetAssetRegistry(), outMessage);
  } else {
    outMessage = std::string(ToString(authoringDocument_.GetMaterial().kind)) +
                 " asset opened for authoring";
  }
  return true;
}

bool ModelMaterialWorkspaceController::CreateMaterial(
    DocumentSceneBase &scene, MaterialTemplateId templateId, bool instance,
    std::string &outMessage) {
  if (authoringDocument_.IsMaterialDirty() ||
      authoringDocument_.IsAbPreviewActive()) {
    outMessage = "save, undo, or discard the current material changes first";
    return false;
  }

  std::string displayName(newMaterialName_.data());
  if (displayName.empty()) {
    displayName = instance ? "New Material Instance" : "New Material";
  }
  MaterialAssetData source{};
  if (instance) {
    if (!authoringDocument_.HasMaterial() ||
        !authoringDocument_.GetMaterialAssetGuid().IsValid()) {
      outMessage = "open a saved parent material before creating an instance";
      return false;
    }
    source.kind = MaterialAssetKind::Instance;
    source.materialName = std::move(displayName);
    source.templateId = authoringDocument_.GetMaterial().templateId;
    source.instance.parentMaterialGuid =
        authoringDocument_.GetMaterialAssetGuid();
  } else {
    source = ASSETS::MATERIAL::CreateMaterialFromTemplate(templateId,
                                                          displayName);
  }

  ASSETS::MATERIAL::MaterialAssetAuthoringService service(
      scene.GetAssetDatabase());
  const ASSETS::MATERIAL::MaterialAuthoringResult created =
      service.Create(source);
  outMessage = created.message;
  if (!created.success) {
    return false;
  }
  authoringDocument_.OpenMaterial(
      created.assetGuid, created.sourcePath, std::move(source), created.compile,
      created.hasLastGoodProgram ? &created.lastGoodProgram : nullptr, true);
  candidateMaterialGuid_ = created.assetGuid;
  candidateAuthoringAssetGuid_ = created.assetGuid;
  instanceParentCache_.reset();
  instanceParentCacheGuid_ = {};
  instanceParentCacheRevision_ = 0u;
  (void)scene.ReloadAssets();

  const ModelMaterialSlotView *slot = GetSelectedSlot();
  if (slot != nullptr && slot->slotId.IsValid()) {
    ModelMaterialBindingCommand command{};
    command.type = ModelMaterialBindingCommandType::BindMaterialAsset;
    command.slotId = slot->slotId;
    command.materialIndexHint = slot->materialIndex;
    command.materialAssetGuid = created.assetGuid;
    const ModelMaterialBindingCommandResult binding = ExecuteBindingCommand(
        command, scene.GetAssetDatabase(), scene.GetAssetRegistry());
    if (!binding.message.empty()) {
      outMessage += "; " + binding.message;
    }
  }
  (void)RebuildAuthoredPreview(scene.GetAssetDatabase(),
                               scene.GetAssetRegistry(), outMessage);
  return true;
}

bool ModelMaterialWorkspaceController::CreateAuxiliaryMaterialAsset(
    DocumentSceneBase &scene, MaterialAssetKind kind,
    std::string &outMessage) {
  if (kind != MaterialAssetKind::Ramp &&
      kind != MaterialAssetKind::Function &&
      kind != MaterialAssetKind::Profile) {
    outMessage = "unsupported auxiliary material asset kind";
    return false;
  }
  if (authoringDocument_.IsMaterialDirty() ||
      authoringDocument_.IsAbPreviewActive()) {
    outMessage = "save, undo, or discard the current material changes first";
    return false;
  }

  MaterialAssetData source{};
  source.kind = kind;
  source.templateId = kind == MaterialAssetKind::Profile
                          ? selectedTemplate_
                          : MaterialTemplateId::Custom;
  const std::string requestedName(newMaterialName_.data());
  source.materialName = requestedName.empty()
                            ? std::string("New ") +
                                  std::string(ToString(kind))
                            : requestedName;
  if (kind == MaterialAssetKind::Ramp) {
    source.ramp.dimension = MaterialRampDimension::OneD;
    source.ramp.interpolation = MaterialRampInterpolation::Smooth;
    source.ramp.width = 256u;
    source.ramp.height = 1u;
    MaterialRampPointData shadow{};
    shadow.id = "ramp.shadow";
    shadow.position = {0.0f, 0.0f};
    shadow.color = {0.42f, 0.48f, 0.66f, 1.0f};
    MaterialRampPointData light{};
    light.id = "ramp.light";
    light.position = {1.0f, 0.0f};
    light.color = {1.0f, 1.0f, 1.0f, 1.0f};
    source.ramp.points = {std::move(shadow), std::move(light)};
  } else if (kind == MaterialAssetKind::Profile) {
    source.profile.baseTemplate = selectedTemplate_;
  }

  ASSETS::MATERIAL::MaterialAssetAuthoringService service(
      scene.GetAssetDatabase());
  const ASSETS::MATERIAL::MaterialAuthoringResult created =
      service.Create(source);
  outMessage = created.message;
  if (!created.success) {
    return false;
  }
  authoringDocument_.OpenMaterial(
      created.assetGuid, created.sourcePath, std::move(source),
      created.compile,
      created.hasLastGoodProgram ? &created.lastGoodProgram : nullptr, true);
  candidateAuthoringAssetGuid_ = created.assetGuid;
  selectedGraphNodeId_.clear();
  selectedGraphEdgeId_.clear();
  selectedRampPointId_.clear();
  materialGesture_.reset();
  (void)scene.ReloadAssets();
  outMessage = std::string(ToString(kind)) +
               " asset created and opened";
  return true;
}

bool ModelMaterialWorkspaceController::CreateCharacterMaterialSet(
    DocumentSceneBase &scene, std::string &outMessage) {
  const std::string prefix = newMaterialName_[0] != '\0'
                                 ? std::string(newMaterialName_.data())
                                 : std::string("Character");
  const ASSETS::MATERIAL::DefaultCharacterMaterialSet defaults =
      ASSETS::MATERIAL::CreateDefaultCharacterMaterialSet(prefix);
  ASSETS::MATERIAL::MaterialAssetAuthoringService service(
      scene.GetAssetDatabase());
  size_t createdCount = 0u;
  AssetGuid firstGuid{};
  for (const ASSETS::MATERIAL::CharacterMaterialSetEntry &entry :
       defaults.entries) {
    const ASSETS::MATERIAL::MaterialAuthoringResult created =
        service.Create(entry.source);
    if (!created.success) {
      outMessage = "created " + std::to_string(createdCount) +
                   " character materials; " + created.message;
      return false;
    }
    if (!firstGuid.IsValid()) {
      firstGuid = created.assetGuid;
    }
    ++createdCount;
  }
  (void)scene.ReloadAssets();
  candidateMaterialGuid_ = firstGuid;
  outMessage = "created " + std::to_string(createdCount) +
               " role-based character materials; bind slots manually";
  if (!defaults.diagnostics.empty()) {
    outMessage += "; " + defaults.diagnostics.front();
  }
  return createdCount == defaults.entries.size();
}

bool ModelMaterialWorkspaceController::SaveMaterial(
    DocumentSceneBase &scene, bool saveAs, std::string &outMessage) {
  if (!authoringDocument_.HasMaterial()) {
    outMessage = "open or create a material before saving";
    return false;
  }
  if (authoringDocument_.IsAbPreviewActive()) {
    outMessage = "Keep or Discard the A/B preview before saving";
    return false;
  }

  ASSETS::MATERIAL::MaterialAssetAuthoringService service(
      scene.GetAssetDatabase());
  const MaterialProgramIR *lastGood =
      authoringDocument_.GetLastGoodProgram();
  ASSETS::MATERIAL::MaterialAuthoringResult saved{};
  if (saveAs) {
    std::filesystem::path destination(saveAsPath_.data());
    if (destination.empty()) {
      destination = std::filesystem::path("Materials") /
                    (authoringDocument_.GetMaterial().materialName + "_Copy");
    }
    saved = service.SaveAs(authoringDocument_.GetMaterial(), destination,
                           lastGood);
  } else if (authoringDocument_.GetMaterialAssetGuid().IsValid()) {
    saved = service.Save(authoringDocument_.GetMaterialAssetGuid(),
                         authoringDocument_.GetMaterial(), lastGood);
  } else {
    saved = service.Create(authoringDocument_.GetMaterial());
  }
  outMessage = saved.message;
  if (!saved.success) {
    return false;
  }

  const AssetGuid previousGuid = authoringDocument_.GetMaterialAssetGuid();
  authoringDocument_.MarkMaterialSaved(saved.assetGuid, saved.sourcePath);
  candidateAuthoringAssetGuid_ = saved.assetGuid;
  if (IsRenderableMaterialAsset(authoringDocument_.GetMaterial().kind)) {
    candidateMaterialGuid_ = saved.assetGuid;
  }
  const bool registryReloaded = scene.ReloadAssets();
  if (!registryReloaded) {
    outMessage += "; editor registry refresh failed";
  } else {
    (void)scene.RebuildMaterialOverridesForMaterial(saved.assetGuid);
  }
  if (saveAs && previousGuid != saved.assetGuid) {
    const ModelMaterialSlotView *slot = GetSelectedSlot();
    if (slot != nullptr && slot->overrideMaterialGuid == previousGuid) {
      ModelMaterialBindingCommand command{};
      command.type = ModelMaterialBindingCommandType::BindMaterialAsset;
      command.slotId = slot->slotId;
      command.materialIndexHint = slot->materialIndex;
      command.materialAssetGuid = saved.assetGuid;
      (void)ExecuteBindingCommand(command, scene.GetAssetDatabase(),
                                  scene.GetAssetRegistry());
    }
  }
  if (IsRenderableMaterialAsset(authoringDocument_.GetMaterial().kind)) {
    (void)RebuildAuthoredPreview(scene.GetAssetDatabase(),
                                 scene.GetAssetRegistry(), outMessage);
  }
  return true;
}

bool ModelMaterialWorkspaceController::RebuildAuthoredPreview(
    AssetDatabase &assetDatabase, const AssetRegistry &assetRegistry,
    std::string &outMessage) {
  if (!authoringDocument_.HasMaterial()) {
    return false;
  }
  if (!IsRenderableMaterialAsset(authoringDocument_.GetMaterial().kind)) {
    outMessage = std::string(ToString(authoringDocument_.GetMaterial().kind)) +
                 " saved; auxiliary assets do not replace slot preview";
    return true;
  }
  const ModelMaterialSlotView *selected = GetSelectedSlot();
  if (selected == nullptr) {
    outMessage = "select a model material slot to preview this material";
    return false;
  }
  SlotState *slot = FindSlot(selected->slotId);
  if (slot == nullptr) {
    outMessage = "selected model material slot is no longer available";
    return false;
  }

  MaterialAssetData effective = authoringDocument_.GetMaterial();
  if (effective.kind == MaterialAssetKind::Instance) {
    ASSETS::MATERIAL::MaterialAssetAuthoringService service(assetDatabase);
    const ASSETS::MATERIAL::MaterialInstanceResolveResult resolved =
        service.ResolveInstance(effective);
    if (resolved.success) {
      effective = resolved.resolvedMaterial;
    } else {
      outMessage = "material instance resolution failed; last preview kept";
      return false;
    }
  }

  auto previewMaterial = std::make_shared<Material>();
  MaterialRuntimeBuilder builder{};
  RENDER3D::MATERIAL::MaterialProgramResolution resolution{};
  const std::string identity =
      authoringDocument_.GetMaterialAssetGuid().IsValid()
          ? authoringDocument_.GetMaterialAssetGuid().value
          : std::string("ModelMaterial/Unsaved");
  if (!builder.BuildRuntimeMaterial(effective, assetRegistry,
                                    materialProgramRegistry_, *previewMaterial,
                                    identity, &resolution)) {
    outMessage = "material preview projection failed; last preview kept";
    return false;
  }
  if (!previewScene_.SetPreviewMaterialOverride(slot->view.materialIndex,
                                                previewMaterial.get())) {
    outMessage = "preview rejected the authored material slot";
    return false;
  }
  slot->previewMaterial = std::move(previewMaterial);
  slot->view.source = ModelMaterialBindingSource::AuthoredOverride;
  slot->view.diagnostic.clear();
  UpdatePublicSlotViews();
  outMessage = resolution.UsedLastGood()
                   ? "preview uses the last-good material program"
               : resolution.UsedFallback()
                   ? "preview uses the Standard PBR fallback"
                   : "authored material preview updated";
  return true;
}

void ModelMaterialWorkspaceController::BeginMaterialGesture(
    std::string label) {
  if (!authoringDocument_.HasMaterial() || materialGesture_) {
    return;
  }
  materialGesture_ = MaterialEditGesture{authoringDocument_.GetMaterial(),
                                         std::move(label)};
}

void ModelMaterialWorkspaceController::CommitMaterialGesture(
    DocumentSceneBase &scene) {
  if (!materialGesture_) {
    return;
  }
  MaterialEditGesture gesture = std::move(*materialGesture_);
  materialGesture_.reset();
  if (authoringDocument_.CommitMaterial(std::move(gesture.before),
                                        std::move(gesture.label))) {
    (void)RebuildAuthoredPreview(scene.GetAssetDatabase(),
                                 scene.GetAssetRegistry(), statusMessage_);
  }
}

void ModelMaterialWorkspaceController::CancelMaterialGesture(
    DocumentSceneBase &scene) {
  if (!materialGesture_) {
    return;
  }
  authoringDocument_.RestoreMaterial(std::move(materialGesture_->before));
  materialGesture_.reset();
  (void)RebuildAuthoredPreview(scene.GetAssetDatabase(),
                               scene.GetAssetRegistry(), statusMessage_);
  statusMessage_ = "material edit canceled";
}

void ModelMaterialWorkspaceController::CommitMaterialMutation(
    MaterialAssetData before, std::string label,
    DocumentSceneBase &scene) {
  if (authoringDocument_.CommitMaterial(std::move(before), std::move(label))) {
    (void)RebuildAuthoredPreview(scene.GetAssetDatabase(),
                                 scene.GetAssetRegistry(), statusMessage_);
  }
}

bool ModelMaterialWorkspaceController::ApplyHistoryProjection(
    ModelMaterialWorkspaceDocument::ChangedDomain domain,
    AssetDatabase &assetDatabase, const AssetRegistry &assetRegistry,
    std::string &outMessage) {
  if (domain == ModelMaterialWorkspaceDocument::ChangedDomain::Binding) {
    RebuildSlotState(assetDatabase, assetRegistry);
    return true;
  }
  if (domain == ModelMaterialWorkspaceDocument::ChangedDomain::Material) {
    return RebuildAuthoredPreview(assetDatabase, assetRegistry, outMessage);
  }
  return true;
}

} // namespace HIKARI::EDITOR
