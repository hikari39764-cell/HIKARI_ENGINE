#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <string>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Assets/Material/HIKARI_MaterialAssetLookup.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {
namespace {

MaterialTextureSlotData ResolveImportedTextureSlot(const ModelAsset& model, const TextureSlot& slot,
                                                   const AssetDatabase& database, uint32_t& unresolvedCount) {
    MaterialTextureSlotData result{};
    result.texCoord = slot.texCoord;
    result.uvScale = slot.uvScale;
    result.uvOffset = slot.uvOffset;
    result.uvRotation = slot.uvRotation;
    if (slot.textureIndex < 0 || static_cast<size_t>(slot.textureIndex) >= model.textures.size()) {
        return result;
    }

    const TextureAsset3D& texture = model.textures[static_cast<size_t>(slot.textureIndex)];
    const AssetRecord* record = nullptr;
    if (!texture.sourcePath.empty()) {
        record = database.FindByPath(texture.sourcePath);
        if (record == nullptr) {
            record = database.FindByArtifactPath(texture.sourcePath);
        }
    }
    if (record == nullptr && !texture.resolvedPath.empty()) {
        record = database.FindByPath(texture.resolvedPath);
        if (record == nullptr) {
            record = database.FindByArtifactPath(texture.resolvedPath);
        }
    }
    if (record == nullptr || record->type != AssetType::Texture) {
        ++unresolvedCount;
        return result;
    }
    result.textureAssetGuid = record->guid;
    result.useTexture = result.textureAssetGuid.IsValid();
    return result;
}

MaterialAlphaMode ResolveImportedAlphaMode(AlphaMode mode) noexcept {
    switch (mode) {
    case AlphaMode::Mask: return MaterialAlphaMode::Mask;
    case AlphaMode::Blend: return MaterialAlphaMode::Blend;
    case AlphaMode::Opaque: return MaterialAlphaMode::Opaque;
    }
    return MaterialAlphaMode::Opaque;
}

bool BuildImportedMaterialSource(const ModelAsset& model, uint32_t materialIndex, const ModelMaterialSlotView& slot,
                                 const AssetDatabase& database, MaterialTemplateId templateId,
                                 MaterialAssetData& outSource, uint32_t& outUnresolvedTextures,
                                 std::string& outMessage) {
    if (materialIndex >= model.materials.size()) {
        outMessage = "selected imported material is no longer available";
        return false;
    }

    const MaterialAsset& imported = model.materials[materialIndex];
    const std::string displayName =
        imported.name.empty() ? slot.displayName + " Material" : imported.name + " Material";
    outSource = ASSETS::MATERIAL::CreateMaterialFromTemplate(templateId, displayName);
    PbrMaterialAssetData pbr{};
    pbr.version = kMaterialAssetSourceVersion;
    pbr.materialName = displayName;
    pbr.baseColorFactor = imported.baseColorFactor;
    pbr.metallicFactor = imported.metallicFactor;
    pbr.roughnessFactor = imported.roughnessFactor;
    pbr.specularFactor = imported.specularFactor;
    pbr.specularColorFactor = imported.specularColorFactor;
    pbr.normalScale = imported.normalTexture.scale;
    pbr.occlusionStrength = imported.occlusionTexture.strength;
    pbr.emissiveFactor = imported.emissiveFactor;
    pbr.emissiveStrength = imported.emissiveStrength;
    pbr.doubleSided = imported.doubleSided;

    outUnresolvedTextures = 0u;
    pbr.baseColorTexture =
        ResolveImportedTextureSlot(model, imported.baseColorTexture, database, outUnresolvedTextures);
    pbr.normalTexture = ResolveImportedTextureSlot(model, imported.normalTexture, database, outUnresolvedTextures);
    pbr.metallicRoughnessTexture =
        ResolveImportedTextureSlot(model, imported.metallicRoughnessTexture, database, outUnresolvedTextures);
    pbr.occlusionTexture =
        ResolveImportedTextureSlot(model, imported.occlusionTexture, database, outUnresolvedTextures);
    pbr.emissiveTexture = ResolveImportedTextureSlot(model, imported.emissiveTexture, database, outUnresolvedTextures);
    pbr.specularTexture = ResolveImportedTextureSlot(model, imported.specularTexture, database, outUnresolvedTextures);
    pbr.specularColorTexture =
        ResolveImportedTextureSlot(model, imported.specularColorTexture, database, outUnresolvedTextures);
    ApplyPbrMaterialAssetData(outSource, pbr);
    outSource.surface.alphaMode = ResolveImportedAlphaMode(imported.alphaMode);
    outSource.surface.alphaCutoff = imported.alphaCutoff;
    return true;
}

} // namespace

bool ModelMaterialWorkspaceController::OpenImportedMaterialDraft(DocumentSceneBase& scene, uint32_t materialIndex,
                                                                 std::string& outMessage) {
    const ModelMaterialSlotView* slot = GetSelectedSlot();
    const ModelAsset* model = previewScene_.GetModel();
    if (slot == nullptr || slot->materialIndex != materialIndex || model == nullptr) {
        outMessage = "select an imported model material slot first";
        return false;
    }
    if (authoringDocument_.IsMaterialDirty() || authoringDocument_.IsAbPreviewActive()) {
        outMessage = "save, undo, or discard the current material changes first";
        return false;
    }

    MaterialAssetData source{};
    uint32_t unresolvedTextures = 0u;
    if (!BuildImportedMaterialSource(*model, materialIndex, *slot, scene.GetAssetDatabase(),
                                     MaterialTemplateId::StandardPbr, source, unresolvedTextures, outMessage)) {
        return false;
    }
    const MaterialCompileOptions compileOptions =
        ASSETS::MATERIAL::MakeMaterialCompileOptions(scene.GetAssetDatabase());
    const MaterialCompileResult compile = CompileMaterialAsset(source, compileOptions);
    authoringDocument_.SetMaterialCompileOptions(compileOptions);
    nodePreviewId_.clear();
    nodePreviewPin_.clear();
    authoringDocument_.OpenMaterial({}, {}, std::move(source), compile, true);
    RefreshAuthoringIdCounter();
    candidateAuthoringAssetGuid_ = {};
    candidateMaterialGuid_ = {};
    selectedGraphNodeId_.clear();
    selectedGraphEdgeId_.clear();
    selectedRampPointId_.clear();
    authoringDocument_.EndMaterialGesture(true);
    instanceParentCache_.reset();
    instanceParentCacheGuid_ = {};
    instanceParentCacheRevision_ = 0u;
    outMessage = "editing imported material as an unsaved draft; selection keeps the "
                 "original runtime material until the first actual edit";
    if (unresolvedTextures != 0u) {
        outMessage +=
            "; " + std::to_string(unresolvedTextures) + " imported texture(s) are not registered Texture Assets";
    }
    return true;
}

bool ModelMaterialWorkspaceController::RestoreSelectedImportedMaterial(DocumentSceneBase& scene,
                                                                       std::string& outMessage) {
    const ModelMaterialSlotView* slot = GetSelectedSlot();
    if (slot == nullptr || !slot->slotId.IsValid()) {
        outMessage = "select a model part before restoring its imported material";
        return false;
    }
    const uint32_t materialIndex = slot->materialIndex;
    ModelMaterialBindingCommand command{};
    command.type = ModelMaterialBindingCommandType::RestoreImportedFallback;
    command.slotId = slot->slotId;
    command.materialIndexHint = materialIndex;
    const ModelMaterialBindingCommandResult restored =
        ExecuteBindingCommand(command, scene.GetAssetDatabase(), scene.GetAssetRegistry());
    if (!restored.accepted) {
        outMessage = restored.message;
        return false;
    }

    authoringDocument_.CloseMaterial();
    if (!OpenImportedMaterialDraft(scene, materialIndex, outMessage)) {
        return false;
    }
    outMessage = restored.changed ? "restored the imported model material; save the Material Set to persist"
                                  : "this part already uses its imported model material";
    return true;
}

} // namespace HIKARI::EDITOR
