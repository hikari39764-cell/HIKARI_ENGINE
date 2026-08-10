#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <cstdio>
#include <filesystem>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/Material/Authoring/HIKARI_MaterialAssetAuthoringService.h"
#include "Assets/Material/Authoring/HIKARI_MaterialInstanceResolver.h"
#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Assets/Material/HIKARI_MaterialAssetLookup.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Material/HIKARI_MaterialRuntimeBuilder.h"
#include "Render3D/Material/Program/HIKARI_MaterialExpressionProgram.h"
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
  if (!executingPendingTransition_ &&
      (authoringDocument_.IsMaterialDirty() ||
       authoringDocument_.IsAbPreviewActive())) {
    PendingTransition transition{};
    transition.kind = PendingTransitionKind::OpenMaterial;
    transition.assetGuid = guid;
    QueuePendingTransition(std::move(transition), outMessage);
    return false;
  }

  ASSETS::MATERIAL::MaterialAssetAuthoringService service(
      scene.GetAssetDatabase());
  ASSETS::MATERIAL::MaterialAuthoringDocument loaded{};
  if (!service.Load(guid, loaded, outMessage)) {
    return false;
  }
  authoringDocument_.SetMaterialCompileOptions(
      ASSETS::MATERIAL::MakeMaterialCompileOptions(
          scene.GetAssetDatabase()));
  EnsureBuiltInRampPresets(scene);
  RefreshAssetChoices(scene.GetAssetDatabase());
  const bool addedDefaultRampBindings =
      BindBuiltInRampPresets(loaded.source);
  if (addedDefaultRampBindings) {
    loaded.compile = CompileMaterialAsset(
        loaded.source,
        ASSETS::MATERIAL::MakeMaterialCompileOptions(
            scene.GetAssetDatabase()));
  }
  authoringDocument_.OpenMaterial(
      loaded.assetGuid, loaded.sourcePath, std::move(loaded.source),
      loaded.compile,
      !addedDefaultRampBindings);
  RefreshAuthoringIdCounter();
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
    if (addedDefaultRampBindings) {
      outMessage += "; missing Ramp slots received project defaults (Save to persist)";
    }
  } else {
    outMessage = std::string(ToString(authoringDocument_.GetMaterial().kind)) +
                 " asset opened for authoring";
  }
  return true;
}

bool ModelMaterialWorkspaceController::CreateMaterial(
    DocumentSceneBase &scene, MaterialTemplateId templateId, bool instance,
    std::string &outMessage) {
  if (!executingPendingTransition_ &&
      (authoringDocument_.IsMaterialDirty() ||
       authoringDocument_.IsAbPreviewActive())) {
    PendingTransition transition{};
    transition.kind = PendingTransitionKind::CreateMaterial;
    transition.templateId = templateId;
    transition.instance = instance;
    QueuePendingTransition(std::move(transition), outMessage);
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
    EnsureBuiltInRampPresets(scene);
    RefreshAssetChoices(scene.GetAssetDatabase());
    BindBuiltInRampPresets(source);
  }

  return CreateAndBindRenderableMaterial(
      scene, std::move(source), outMessage);
}

bool ModelMaterialWorkspaceController::CreateAndBindRenderableMaterial(
    DocumentSceneBase &scene, MaterialAssetData source,
    std::string &outMessage) {

  const MaterialDomain createdDomain = source.domain;

  ASSETS::MATERIAL::MaterialAssetAuthoringService service(
      scene.GetAssetDatabase());
  const ASSETS::MATERIAL::MaterialAuthoringResult created =
      service.Create(source);
  outMessage = created.message;
  if (!created.success) {
    return false;
  }
  authoringDocument_.SetMaterialCompileOptions(
      ASSETS::MATERIAL::MakeMaterialCompileOptions(
          scene.GetAssetDatabase()));
  authoringDocument_.OpenMaterial(
      created.assetGuid, created.sourcePath, std::move(source), created.compile,
      true);
  RefreshAuthoringIdCounter();
  candidateMaterialGuid_ = created.assetGuid;
  candidateAuthoringAssetGuid_ = created.assetGuid;
  instanceParentCache_.reset();
  instanceParentCacheGuid_ = {};
  instanceParentCacheRevision_ = 0u;
  (void)scene.ReloadAssets();

  const ModelMaterialSlotView *slot =
      authoringMode_ == AuthoringMode::ModelMaterials &&
              createdDomain == MaterialDomain::Surface
          ? GetSelectedSlot()
          : nullptr;
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
  if (createdDomain == MaterialDomain::Particle) {
    outMessage +=
        "; Particle material created; preview it from a VFX emitter";
  } else {
    (void)RebuildAuthoredPreview(scene.GetAssetDatabase(),
                                 scene.GetAssetRegistry(), outMessage);
  }
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
  if (!executingPendingTransition_ &&
      (authoringDocument_.IsMaterialDirty() ||
       authoringDocument_.IsAbPreviewActive())) {
    PendingTransition transition{};
    transition.kind = PendingTransitionKind::CreateAuxiliaryMaterial;
    transition.materialKind = kind;
    QueuePendingTransition(std::move(transition), outMessage);
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
    source = ASSETS::MATERIAL::CreateMaterialRampFromPreset(
        ASSETS::MATERIAL::MaterialRampPresetId::ToonSoft,
        source.materialName);
  } else if (kind == MaterialAssetKind::Function) {
    source = ASSETS::MATERIAL::CreatePassThroughMaterialFunction(
        source.materialName);
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
  authoringDocument_.SetMaterialCompileOptions(
      ASSETS::MATERIAL::MakeMaterialCompileOptions(
          scene.GetAssetDatabase()));
  authoringDocument_.OpenMaterial(
      created.assetGuid, created.sourcePath, std::move(source),
      created.compile, true);
  RefreshAuthoringIdCounter();
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
  if (!authoringDocument_.GetCompileResult().success) {
    outMessage = "Material v6 graph is invalid; fix Compile & Diagnostics before saving";
    return false;
  }

  ASSETS::MATERIAL::MaterialAssetAuthoringService service(
      scene.GetAssetDatabase());
  ASSETS::MATERIAL::MaterialAuthoringResult saved{};
  if (saveAs) {
    std::filesystem::path destination(saveAsPath_.data());
    if (destination.empty()) {
      destination = std::filesystem::path("Materials") /
                    (authoringDocument_.GetMaterial().materialName + "_Copy");
    }
    saved = service.SaveAs(authoringDocument_.GetMaterial(), destination);
  } else if (authoringDocument_.GetMaterialAssetGuid().IsValid()) {
    saved = service.Save(authoringDocument_.GetMaterialAssetGuid(),
                         authoringDocument_.GetMaterial());
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
  } else if (!previousGuid.IsValid() &&
             authoringMode_ == AuthoringMode::ModelMaterials) {
    const ModelMaterialSlotView *slot = GetSelectedSlot();
    if (slot != nullptr && slot->slotId.IsValid()) {
      ModelMaterialBindingCommand command{};
      command.type = ModelMaterialBindingCommandType::BindMaterialAsset;
      command.slotId = slot->slotId;
      command.materialIndexHint = slot->materialIndex;
      command.materialAssetGuid = saved.assetGuid;
      const ModelMaterialBindingCommandResult binding =
          ExecuteBindingCommand(command, scene.GetAssetDatabase(),
                                scene.GetAssetRegistry());
      if (!binding.message.empty()) {
        outMessage += "; " + binding.message;
      }
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
  if (!authoringDocument_.GetCompileResult().success) {
    outMessage = "current Material v6 graph is invalid; preview is paused until diagnostics are fixed";
    return false;
  }
  if (!IsRenderableMaterialAsset(authoringDocument_.GetMaterial().kind)) {
    outMessage = std::string(ToString(authoringDocument_.GetMaterial().kind)) +
                 " saved; auxiliary assets do not replace slot preview";
    return true;
  }
  MaterialAssetData effective = authoringDocument_.GetMaterial();
  MaterialCompileResult effectiveCompile =
      authoringDocument_.GetCompileResult();
  if (effective.kind == MaterialAssetKind::Instance) {
    ASSETS::MATERIAL::MaterialAssetAuthoringService service(assetDatabase);
    const ASSETS::MATERIAL::MaterialInstanceResolveResult resolved =
        service.ResolveInstance(effective);
    if (resolved.success) {
      effective = resolved.resolvedMaterial;
      effectiveCompile = resolved.compile;
    } else {
      outMessage = "material instance resolution failed; last preview kept";
      return false;
    }
  }

  if (effective.domain == MaterialDomain::Particle) {
    standalonePreviewMaterial_.reset();
    outMessage =
        "Particle material graph compiled; use the VFX workspace preview "
        "to evaluate particle UV, time, position, and view inputs";
    return true;
  }

  auto previewMaterial = std::make_shared<Material>();
  MaterialRuntimeBuilder builder{};
  RENDER3D::MATERIAL::MaterialProgramResolution resolution{};
  const std::string identity =
      authoringDocument_.GetMaterialAssetGuid().IsValid()
          ? authoringDocument_.GetMaterialAssetGuid().value
          : std::string("ModelMaterial/Unsaved");
  if (!builder.BuildRuntimeMaterial(effective, effectiveCompile, assetRegistry,
                                    materialProgramRegistry_, *previewMaterial,
                                    identity, &resolution)) {
    outMessage = "material preview projection failed; last preview kept";
    return false;
  }
  RENDER3D::MATERIAL::MaterialExpressionRuntimeProgram previewExpression{};
  std::string previewExpressionError{};
  (void)RENDER3D::MATERIAL::BuildMaterialExpressionRuntimeProgram(
      effectiveCompile.program, previewExpression,
      previewExpressionError);
  const bool hasExpressionBlock =
      previewExpression.active &&
      previewExpression.outputMask != 0u &&
      previewMaterial->GetMaterialProgramId().value != 0u;
  const auto maskText = [](uint32_t mask) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%08X", mask);
    return std::string(buffer);
  };
  materialPreviewExpressionStatus_ =
      hasExpressionBlock
          ? "Expression active mask=0x" +
                maskText(previewExpression.outputMask)
          : "Expression inactive";
  previewMaterial->SetInheritModelTextures(
      authoringMode_ == AuthoringMode::ModelMaterials);
  if (authoringMode_ == AuthoringMode::MaterialAsset) {
    if (!materialSphereScene_.IsReady() ||
        !materialSphereScene_.SetPreviewMaterialOverride(
            0u, previewMaterial.get())) {
      outMessage = "material sphere rejected the authored material";
      return false;
    }
    standalonePreviewMaterial_ = std::move(previewMaterial);
    outMessage = std::string(
                     resolution.UsedFallback()
                         ? "material sphere uses the Standard PBR fallback"
                         : "standalone material preview updated") +
        (hasExpressionBlock
             ? " | expression active mask=0x" +
                   maskText(previewExpression.outputMask)
             : " | expression inactive");
    return true;
  }

  const ModelMaterialSlotView *selected = GetSelectedSlot();
  if (selected == nullptr) {
    outMessage = "select a model part to preview this material";
    return false;
  }
  SlotState *slot = FindSlot(selected->slotId);
  if (slot == nullptr) {
    outMessage = "selected model material slot is no longer available";
    return false;
  }
  if (!previewScene_.SetPreviewMaterialOverride(slot->view.materialIndex,
                                                previewMaterial.get())) {
    outMessage = "preview rejected the authored material slot";
    return false;
  }
  slot->previewMaterial = std::move(previewMaterial);
  const bool savedAsset =
      authoringDocument_.GetMaterialAssetGuid().IsValid();
  slot->view.source = savedAsset
                          ? ModelMaterialBindingSource::AuthoredOverride
                          : ModelMaterialBindingSource::ImportedFallback;
  slot->view.diagnostic = savedAsset
                              ? std::string{}
                              : "unsaved editable copy of imported material";
  UpdatePublicSlotViews();
  RefreshSelectedSlotPresentation();
  outMessage = std::string(
                   resolution.UsedFallback()
                       ? "preview uses the Standard PBR fallback"
                       : "authored material preview updated") +
      (hasExpressionBlock
           ? " | expression active mask=0x" +
                 maskText(previewExpression.outputMask)
           : " | expression inactive");
  return true;
}

bool ModelMaterialWorkspaceController::RefreshMaterialPreviews(
    DocumentSceneBase &scene, std::string &outMessage) {
  materialPreviewRefreshPending_ = false;
  if (authoringDocument_.HasMaterial() &&
      !authoringDocument_.GetCompileResult().success) {
    outMessage =
        "current Material v6 graph is invalid; preview and runtime hot update are paused";
    return false;
  }
  const bool authoredPreviewUpdated = RebuildAuthoredPreview(
      scene.GetAssetDatabase(), scene.GetAssetRegistry(), outMessage);
  if (!authoringDocument_.HasMaterial() ||
      !authoringDocument_.GetMaterialAssetGuid().IsValid() ||
      !IsRenderableMaterialAsset(authoringDocument_.GetMaterial().kind)) {
    return authoredPreviewUpdated;
  }

  const int runtimeBindingCount = scene.ApplyRuntimeMaterialOverridePreview(
      authoringDocument_.GetMaterialAssetGuid(),
      authoringDocument_.GetMaterial());
  if (runtimeBindingCount > 0) {
    outMessage += "; runtime hot preview updated " +
                  std::to_string(runtimeBindingCount) + " binding(s)";
  }
  return authoredPreviewUpdated;
}

} // namespace HIKARI::EDITOR
