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
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {
const MaterialAssetData* ModelMaterialWorkspaceController::LoadInstanceParent(AssetDatabase& database,
                                                                              std::string& outMessage) {
    const auto& guid = authoringDocument_.EditMaterial().instance.parentMaterialGuid;
    if (!instanceParentCache_ || instanceParentCacheGuid_ != guid ||
        instanceParentCacheRevision_ != database.GetContentRevision()) {
        ASSETS::MATERIAL::MaterialAuthoringDocument parent;
        if (!ASSETS::MATERIAL::MaterialAssetAuthoringService(database).Load(guid, parent, outMessage)) {
            instanceParentCache_.reset();
            return nullptr;
        }
        instanceParentCache_ = std::move(parent.source);
        instanceParentCacheGuid_ = guid;
        instanceParentCacheRevision_ = database.GetContentRevision();
    }
    return &*instanceParentCache_;
}

bool ModelMaterialWorkspaceController::EditMaterialParameter(DocumentSceneBase& scene, const std::string& parameterId,
                                                             const MaterialValueData& value, std::string& outMessage,
                                                             std::string& outCode) {
    std::lock_guard<std::recursive_mutex> guard(authoringDocument_.GetMutex());
    if (!authoringDocument_.HasMaterial() || authoringDocument_.HasMaterialGesture()) {
        outCode = authoringDocument_.HasMaterial() ? "MATERIAL_EDIT_ACTIVE" : "MATERIAL_DOCUMENT_MISSING";
        outMessage = "Open a material and finish the active edit first.";
        return false;
    }
    auto material = authoringDocument_.GetMaterial();
    const auto* parent = material.kind == MaterialAssetKind::Instance
                             ? LoadInstanceParent(scene.GetAssetDatabase(), outMessage)
                             : nullptr;
    const auto* graph = parent ? &parent->graph : MATERIAL_GRAPH_UI::EditableGraph(material);
    std::string id = parameterId;
    if (graph && !FindMaterialGraphParameter(*graph, id))
        id = "parameter." + id;
    if (!ASSETS::MATERIAL::SetMaterialParameterValue(material, id, value, outMessage, parent)) {
        outCode = "INVALID_PARAMETER_VALUE";
        return false;
    }
    const bool changed = authoringDocument_.ApplyMaterial(std::move(material), "Edit material parameter");
    if (changed)
        (void)FlushMaterialPreview(scene, statusMessage_);
    outCode = "OK";
    outMessage = (changed ? "Updated parameter: " : "Parameter unchanged: ") + id;
    return true;
}

bool ModelMaterialWorkspaceController::OpenMaterialAsset(DocumentSceneBase& scene, const AssetGuid& guid,
                                                         std::string& outMessage) {
    if (!guid.IsValid()) {
        outMessage = "choose a valid material asset";
        return false;
    }
    if (authoringDocument_.HasMaterial() && authoringDocument_.GetMaterialAssetGuid() == guid) {
        outMessage = "material is already open";
        return true;
    }
    if (!executingPendingTransition_ &&
        (authoringDocument_.IsMaterialDirty() || authoringDocument_.IsAbPreviewActive())) {
        PendingTransition transition{};
        transition.kind = PendingTransitionKind::OpenMaterial;
        transition.assetGuid = guid;
        QueuePendingTransition(std::move(transition), outMessage);
        return false;
    }

    ASSETS::MATERIAL::MaterialAssetAuthoringService service(scene.GetAssetDatabase());
    ASSETS::MATERIAL::MaterialAuthoringDocument loaded{};
    if (!service.Load(guid, loaded, outMessage)) {
        return false;
    }
    authoringDocument_.SetMaterialCompileOptions(
        ASSETS::MATERIAL::MakeMaterialCompileOptions(scene.GetAssetDatabase()));
    EnsureBuiltInRampPresets(scene);
    RefreshAssetChoices(scene.GetAssetDatabase());
    const bool addedDefaultRampBindings = BindBuiltInRampPresets(loaded.source);
    if (addedDefaultRampBindings) {
        loaded.compile =
            CompileMaterialAsset(loaded.source, ASSETS::MATERIAL::MakeMaterialCompileOptions(scene.GetAssetDatabase()));
    }
    nodePreviewId_.clear();
    nodePreviewPin_.clear();
    authoringDocument_.OpenMaterial(loaded.assetGuid, loaded.sourcePath, std::move(loaded.source), loaded.compile,
                                    !addedDefaultRampBindings);
    RefreshAuthoringIdCounter();
    candidateAuthoringAssetGuid_ = guid;
    if (IsRenderableMaterialAsset(authoringDocument_.GetMaterial().kind)) {
        candidateMaterialGuid_ = guid;
    }
    selectedGraphNodeId_.clear();
    selectedGraphEdgeId_.clear();
    selectedRampPointId_.clear();
    authoringDocument_.EndMaterialGesture(true);
    instanceParentCache_.reset();
    instanceParentCacheGuid_ = {};
    instanceParentCacheRevision_ = 0u;
    if (IsRenderableMaterialAsset(authoringDocument_.GetMaterial().kind)) {
        (void)RebuildAuthoredPreview(scene.GetAssetDatabase(), scene.GetAssetRegistry(), outMessage);
        if (addedDefaultRampBindings) {
            outMessage += "; missing Ramp slots received project defaults (Save to persist)";
        }
    } else {
        outMessage = std::string(ToString(authoringDocument_.GetMaterial().kind)) + " asset opened for authoring";
    }
    return true;
}

bool ModelMaterialWorkspaceController::CreateMaterial(DocumentSceneBase& scene, MaterialTemplateId templateId,
                                                      bool instance, std::string& outMessage) {
    if (!executingPendingTransition_ &&
        (authoringDocument_.IsMaterialDirty() || authoringDocument_.IsAbPreviewActive())) {
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
        if (!authoringDocument_.HasMaterial() || !authoringDocument_.GetMaterialAssetGuid().IsValid()) {
            outMessage = "open a saved parent material before creating an instance";
            return false;
        }
        source.kind = MaterialAssetKind::Instance;
        source.materialName = std::move(displayName);
        source.templateId = authoringDocument_.GetMaterial().templateId;
        source.instance.parentMaterialGuid = authoringDocument_.GetMaterialAssetGuid();
    } else {
        if (templateId == MaterialTemplateId::PainterlyShell && authoringMode_ == AuthoringMode::ModelMaterials &&
            authoringDocument_.HasMaterial()) {
            MaterialAssetData seed = authoringDocument_.GetMaterial();
            if (seed.kind == MaterialAssetKind::Instance) {
                const auto resolved = ASSETS::MATERIAL::ResolveMaterialInstance(
                    seed, [&](const AssetGuid& guid, MaterialAssetData& parent, std::string& error) {
                        return ASSETS::MATERIAL::LoadMaterialAssetSource(scene.GetAssetDatabase(), guid, parent, error);
                    });
                if (!resolved.success) {
                    outMessage = "Resolve the current material instance before creating a painterly material.";
                    return false;
                }
                seed = resolved.resolvedMaterial;
            }
            source = ASSETS::MATERIAL::CreateMaterialFromTemplateWithPbr(templateId, ProjectPbrMaterialAssetData(seed),
                                                                         displayName);
        } else {
            source = ASSETS::MATERIAL::CreateMaterialFromTemplate(templateId, displayName);
        }
        if (templateId == MaterialTemplateId::PainterlyShell)
            (void)MATERIAL_GRAPH_UI::AutoLayoutMaterialGraph(source.graph);
        EnsureBuiltInRampPresets(scene);
        RefreshAssetChoices(scene.GetAssetDatabase());
        BindBuiltInRampPresets(source);
    }

    return CreateAndBindRenderableMaterial(scene, std::move(source), outMessage);
}

bool ModelMaterialWorkspaceController::CreateAndBindRenderableMaterial(DocumentSceneBase& scene,
                                                                       MaterialAssetData source,
                                                                       std::string& outMessage) {
    const MaterialDomain createdDomain = source.domain;

    ASSETS::MATERIAL::MaterialAssetAuthoringService service(scene.GetAssetDatabase());
    const ASSETS::MATERIAL::MaterialAuthoringResult created = service.Create(source);
    outMessage = created.message;
    if (!created.success) {
        return false;
    }
    authoringDocument_.SetMaterialCompileOptions(
        ASSETS::MATERIAL::MakeMaterialCompileOptions(scene.GetAssetDatabase()));
    nodePreviewId_.clear();
    nodePreviewPin_.clear();
    authoringDocument_.OpenMaterial(created.assetGuid, created.sourcePath, std::move(source), created.compile, false);
    RefreshAuthoringIdCounter();
    candidateMaterialGuid_ = created.assetGuid;
    candidateAuthoringAssetGuid_ = created.assetGuid;
    instanceParentCache_.reset();
    instanceParentCacheGuid_ = {};
    instanceParentCacheRevision_ = 0u;
    if (!scene.ReloadAssets())
        return (outMessage += "; Registry publication failed", false);
    authoringDocument_.MarkMaterialSaved(created.assetGuid, created.sourcePath);
    const ModelMaterialSlotView* slot =
        authoringMode_ == AuthoringMode::ModelMaterials && createdDomain == MaterialDomain::Surface ? GetSelectedSlot()
                                                                                                    : nullptr;
    if (slot != nullptr && slot->slotId.IsValid()) {
        ModelMaterialBindingCommand command{};
        command.type = ModelMaterialBindingCommandType::BindMaterialAsset;
        command.slotId = slot->slotId;
        command.materialIndexHint = slot->materialIndex;
        command.materialAssetGuid = created.assetGuid;
        const ModelMaterialBindingCommandResult binding =
            ExecuteBindingCommand(command, scene.GetAssetDatabase(), scene.GetAssetRegistry());
        if (!binding.message.empty()) {
            outMessage += "; " + binding.message;
        }
    }
    if (createdDomain == MaterialDomain::Particle) {
        outMessage += "; Particle material created; preview it from a VFX emitter";
    } else {
        (void)RebuildAuthoredPreview(scene.GetAssetDatabase(), scene.GetAssetRegistry(), outMessage);
    }
    return true;
}

bool ModelMaterialWorkspaceController::CreateAuxiliaryMaterialAsset(DocumentSceneBase& scene, MaterialAssetKind kind,
                                                                    std::string& outMessage) {
    if (kind != MaterialAssetKind::Ramp && kind != MaterialAssetKind::Function && kind != MaterialAssetKind::Profile) {
        outMessage = "unsupported auxiliary material asset kind";
        return false;
    }
    if (!executingPendingTransition_ &&
        (authoringDocument_.IsMaterialDirty() || authoringDocument_.IsAbPreviewActive())) {
        PendingTransition transition{};
        transition.kind = PendingTransitionKind::CreateAuxiliaryMaterial;
        transition.materialKind = kind;
        QueuePendingTransition(std::move(transition), outMessage);
        return false;
    }

    MaterialAssetData source{};
    source.kind = kind;
    source.templateId = kind == MaterialAssetKind::Profile ? selectedTemplate_ : MaterialTemplateId::Custom;
    const std::string requestedName(newMaterialName_.data());
    source.materialName = requestedName.empty() ? std::string("New ") + std::string(ToString(kind)) : requestedName;
    if (kind == MaterialAssetKind::Ramp) {
        source = ASSETS::MATERIAL::CreateMaterialRampFromPreset(ASSETS::MATERIAL::MaterialRampPresetId::ToonSoft,
                                                                source.materialName);
    } else if (kind == MaterialAssetKind::Function) {
        source = ASSETS::MATERIAL::CreatePassThroughMaterialFunction(source.materialName);
    } else if (kind == MaterialAssetKind::Profile) {
        source.profile.baseTemplate = selectedTemplate_;
    }

    ASSETS::MATERIAL::MaterialAssetAuthoringService service(scene.GetAssetDatabase());
    const ASSETS::MATERIAL::MaterialAuthoringResult created = service.Create(source);
    outMessage = created.message;
    if (!created.success) {
        return false;
    }
    authoringDocument_.SetMaterialCompileOptions(
        ASSETS::MATERIAL::MakeMaterialCompileOptions(scene.GetAssetDatabase()));
    nodePreviewId_.clear();
    nodePreviewPin_.clear();
    authoringDocument_.OpenMaterial(created.assetGuid, created.sourcePath, std::move(source), created.compile, false);
    RefreshAuthoringIdCounter();
    candidateAuthoringAssetGuid_ = created.assetGuid;
    selectedGraphNodeId_.clear();
    selectedGraphEdgeId_.clear();
    selectedRampPointId_.clear();
    authoringDocument_.EndMaterialGesture(true);
    if (!scene.ReloadAssets())
        return (outMessage += "; Registry publication failed", false);
    authoringDocument_.MarkMaterialSaved(created.assetGuid, created.sourcePath);
    outMessage = std::string(ToString(kind)) + " asset created and opened";
    return true;
}
bool ModelMaterialWorkspaceController::SaveMaterial(DocumentSceneBase& scene, bool saveAs, std::string& outMessage) {
    authoringDocument_.EndMaterialGesture(false);
    if (!authoringDocument_.HasMaterial()) {
        outMessage = "open or create a material before saving";
        return false;
    }
    if (authoringDocument_.IsAbPreviewActive()) {
        outMessage = "Keep or Discard the A/B preview before saving";
        return false;
    }
    if (!authoringDocument_.GetCompileResult().success) {
        outMessage = "Material graph is invalid; fix Compile & Diagnostics before saving";
        return false;
    }

    ASSETS::MATERIAL::MaterialAssetAuthoringService service(scene.GetAssetDatabase());
    ASSETS::MATERIAL::MaterialAuthoringResult saved{};
    if (saveAs) {
        std::filesystem::path destination(saveAsPath_.data());
        if (destination.empty()) {
            destination =
                std::filesystem::path("Materials") / (authoringDocument_.GetMaterial().materialName + "_Copy");
        }
        saved = service.SaveAs(authoringDocument_.GetMaterial(), destination);
    } else if (authoringDocument_.GetMaterialAssetGuid().IsValid()) {
        if (!authoringDocument_.CanSaveMaterialSource(outMessage))
            return false;
        saved = service.Save(authoringDocument_.GetMaterialAssetGuid(), authoringDocument_.GetMaterial());
    } else {
        saved = service.Create(authoringDocument_.GetMaterial());
    }
    outMessage = saved.message;
    if (!saved.success) {
        return false;
    }

    const AssetGuid previousGuid = authoringDocument_.GetMaterialAssetGuid();
    authoringDocument_.AcceptMaterialSourceWrite(saved.sourcePath);
    if (!scene.ReloadAssets()) {
        outMessage += "; editor registry refresh failed";
        return false;
    }
    authoringDocument_.MarkMaterialSaved(saved.assetGuid, saved.sourcePath);
    candidateAuthoringAssetGuid_ = saved.assetGuid;
    if (IsRenderableMaterialAsset(authoringDocument_.GetMaterial().kind)) {
        candidateMaterialGuid_ = saved.assetGuid;
    }
    (void)scene.RebuildMaterialOverridesForMaterial(saved.assetGuid);
    if (saveAs && previousGuid != saved.assetGuid) {
        const ModelMaterialSlotView* slot = GetSelectedSlot();
        if (slot != nullptr && slot->overrideMaterialGuid == previousGuid) {
            ModelMaterialBindingCommand command{};
            command.type = ModelMaterialBindingCommandType::BindMaterialAsset;
            command.slotId = slot->slotId;
            command.materialIndexHint = slot->materialIndex;
            command.materialAssetGuid = saved.assetGuid;
            (void)ExecuteBindingCommand(command, scene.GetAssetDatabase(), scene.GetAssetRegistry());
        }
    } else if (!previousGuid.IsValid() && authoringMode_ == AuthoringMode::ModelMaterials) {
        const ModelMaterialSlotView* slot = GetSelectedSlot();
        if (slot != nullptr && slot->slotId.IsValid()) {
            ModelMaterialBindingCommand command{};
            command.type = ModelMaterialBindingCommandType::BindMaterialAsset;
            command.slotId = slot->slotId;
            command.materialIndexHint = slot->materialIndex;
            command.materialAssetGuid = saved.assetGuid;
            const ModelMaterialBindingCommandResult binding =
                ExecuteBindingCommand(command, scene.GetAssetDatabase(), scene.GetAssetRegistry());
            if (!binding.message.empty()) {
                outMessage += "; " + binding.message;
            }
        }
    }
    if (IsRenderableMaterialAsset(authoringDocument_.GetMaterial().kind)) {
        (void)RebuildAuthoredPreview(scene.GetAssetDatabase(), scene.GetAssetRegistry(), outMessage);
    }
    return true;
}

} // namespace HIKARI::EDITOR
