#include "Assets/Shader/HIKARI_ShaderRegistryResolve.h"

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"

namespace HIKARI::ASSETS::SHADER {

ShaderAssetResolveResult ResolveShaderAssetByGuid(const AssetRegistry& registry, const AssetGuid& assetGuid,
                                                  const ShaderAssetEntryExpectation& entry,
                                                  const ShaderAssetContractExpectation& contract) {
    ShaderAssetResolveResult request = ShaderAssetResolver::ValidateRequest(entry, contract);
    if (request.code != ShaderAssetResolveCode::Success) {
        return request;
    }
    if (!IsValidAssetGuid(assetGuid.value)) {
        return ShaderAssetResolveResult::Failure(ShaderAssetResolveCode::InvalidRequest,
                                                 "shader asset GUID is invalid");
    }

    const AssetDescriptor* base = registry.FindDescriptor(assetGuid.value);
    if (base == nullptr) {
        return ShaderAssetResolveResult::Failure(ShaderAssetResolveCode::AssetNotFound,
                                                 "shader asset is not registered");
    }
    if (base->type != AssetType::Shader) {
        return ShaderAssetResolveResult::Failure(ShaderAssetResolveCode::AssetTypeMismatch,
                                                 "registered asset is not a Shader");
    }
    const auto* descriptor = dynamic_cast<const ShaderAssetDescriptor*>(base);
    if (descriptor == nullptr) {
        return ShaderAssetResolveResult::Failure(ShaderAssetResolveCode::AssetTypeMismatch,
                                                 "Shader descriptor type is invalid");
    }
    if (descriptor->program == nullptr) {
        return ShaderAssetResolveResult::Failure(ShaderAssetResolveCode::ArtifactUnavailable,
                                                 "Shader descriptor has no cooked artifact");
    }
    if (descriptor->id.value != descriptor->program->sourceGuid.value) {
        return ShaderAssetResolveResult::Failure(ShaderAssetResolveCode::ArtifactInvalid,
                                                 "Shader descriptor and artifact GUIDs differ");
    }
    return ShaderAssetResolver::ResolveArtifact(descriptor->program, entry, contract);
}

} // namespace HIKARI::ASSETS::SHADER
