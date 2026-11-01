#include "Assets/Sprite/HIKARI_SpriteMaterialStore.h"

#include <memory>

#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "RenderContracts/Shader/HIKARI_ProjectSpriteShaderContract.h"
#include "Core/Text/HIKARI_UtfPath.h"

namespace HIKARI::SPRITE {

void SpriteMaterialStore::Configure(const AssetDatabase* database, const AssetRegistry* registry) {
    database_ = database;
    registry_ = registry;
    cache_.Configure(database);
}

std::shared_ptr<const SpriteMaterialAssetData> SpriteMaterialStore::Load(const AssetGuid& guid,
                                                                         std::string& outError) const {
    if (database_ == nullptr || !guid.IsValid()) {
        outError = "SpriteMaterial database is not bound or its GUID is invalid.";
        return {};
    }
    const AssetRecord* record = database_->FindByGuid(guid);
    if (record == nullptr || record->duplicateGuid || record->type != AssetType::SpriteMaterial) {
        outError = "SpriteMaterial GUID is missing, duplicated, or belongs to a different asset type: " + guid.value;
        return {};
    }
    if (!record->lastImportSucceeded) {
        outError = "SpriteMaterial has not imported successfully: " + record->lastImportMessage;
        return {};
    }
    const std::string artifactPath =
        ASSETS::SEMANTICS::FindAssetArtifactPath(*record, ASSETS::SEMANTICS::AssetArtifactKind::SpriteMaterialRuntime);
    if (artifactPath.empty()) {
        outError = "SpriteMaterial runtime artifact is missing: " + guid.value;
        return {};
    }

    std::shared_ptr<const SpriteMaterialAssetData> asset =
        cache_.Load(guid, TEXT::PathFromUtf8(artifactPath), outError);
    if (!asset) {
        return {};
    }
    if (asset->sourceAssetGuid != guid) {
        outError = "SpriteMaterial runtime artifact source GUID does not match its asset record: " + guid.value;
        return {};
    }
    outError.clear();
    return asset;
}

std::shared_ptr<const SpriteMaterialBinding> SpriteMaterialStore::Resolve(const AssetGuid& guid,
                                                                          std::string& outError) const {
    if (registry_ == nullptr) {
        outError = "SpriteMaterial registry is not bound.";
        return {};
    }
    std::shared_ptr<const SpriteMaterialAssetData> asset = Load(guid, outError);
    if (!asset) {
        return {};
    }
    const AssetGuid shaderId{asset->shaderAssetGuid.value};
    const ShaderAssetDescriptor* shaderDescriptor = registry_->FindAs<ShaderAssetDescriptor>(shaderId);
    if (shaderDescriptor == nullptr || !shaderDescriptor->program ||
        shaderDescriptor->program->sourceGuid != asset->shaderAssetGuid) {
        outError =
            "SpriteMaterial Shader is missing or has no current cooked artifact: " + asset->shaderAssetGuid.value;
        return {};
    }
    if (!ASSETS::SHADER::ValidateProjectSpriteShaderArtifact(*shaderDescriptor->program, outError)) {
        return {};
    }

    ASSETS::SHADER::ShaderParameterSlots parameterSlots{};
    if (!ASSETS::SHADER::PackShaderParameterValues(shaderDescriptor->program->parameters, asset->parameters,
                                                   parameterSlots, outError)) {
        return {};
    }
    auto binding = std::make_shared<SpriteMaterialBinding>();
    binding->materialAssetGuid = guid;
    binding->shaderAssetGuid = asset->shaderAssetGuid;
    binding->asset = std::move(asset);
    binding->shaderArtifact = shaderDescriptor->program;
    binding->parameterSlots = parameterSlots;
    outError.clear();
    return binding;
}

} // namespace HIKARI::SPRITE
