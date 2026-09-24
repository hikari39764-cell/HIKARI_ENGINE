#include "Assets/Shader/HIKARI_ShaderAssetResolver.h"

#include <utility>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"

namespace HIKARI::ASSETS::SHADER {
namespace {

ShaderAssetResolveResult Failure(ShaderAssetResolveCode code, std::string diagnostic) {
    ShaderAssetResolveResult result{};
    result.code = code;
    result.diagnostic = std::move(diagnostic);
    return result;
}

bool IsValidEntryExpectation(const ShaderAssetEntryExpectation& entry) noexcept {
    return !entry.entryId.empty() && entry.entryId.size() <= kMaximumShaderIdentifierBytes &&
           entry.entryId.find('\0') == std::string::npos && entry.stage < ShaderStage::Count &&
           !entry.profile.empty() && entry.profile.size() <= kMaximumShaderIdentifierBytes &&
           entry.profile.find('\0') == std::string::npos;
}

bool IsValidContractExpectation(const ShaderAssetContractExpectation& contract) noexcept {
    return !contract.engineInterfaceId.empty() && contract.engineInterfaceId.size() <= kMaximumShaderIdentifierBytes &&
           contract.engineInterfaceId.find('\0') == std::string::npos && contract.engineInterfaceVersion != 0u &&
           !contract.engineInterfaceHash.IsZero() && !contract.rootLayoutId.empty() &&
           contract.rootLayoutId.size() <= kMaximumShaderIdentifierBytes &&
           contract.rootLayoutId.find('\0') == std::string::npos && contract.rootLayoutVersion != 0u &&
           !contract.rootLayoutHash.IsZero() && contract.availableDeviceCaps.shaderModel >= 60u &&
           contract.availableDeviceCaps.rootSignatureVersion >= 10u &&
           (contract.availableDeviceCaps.featureMask & ~ShaderDeviceFeature::All) == 0u;
}

ShaderAssetResolveResult ValidateRequest(const ShaderAssetEntryExpectation& entry,
                                         const ShaderAssetContractExpectation& contract) {
    if (!IsValidEntryExpectation(entry)) {
        return Failure(ShaderAssetResolveCode::InvalidRequest, "shader entry expectation is invalid");
    }
    if (!IsValidContractExpectation(contract)) {
        return Failure(ShaderAssetResolveCode::InvalidRequest, "shader contract expectation is invalid");
    }
    ShaderAssetResolveResult result{};
    result.code = ShaderAssetResolveCode::Success;
    return result;
}

} // namespace

const char* ToString(ShaderAssetResolveCode code) noexcept {
    switch (code) {
    case ShaderAssetResolveCode::Success: return "Success";
    case ShaderAssetResolveCode::InvalidRequest: return "InvalidRequest";
    case ShaderAssetResolveCode::AssetNotFound: return "AssetNotFound";
    case ShaderAssetResolveCode::AssetTypeMismatch: return "AssetTypeMismatch";
    case ShaderAssetResolveCode::ArtifactUnavailable: return "ArtifactUnavailable";
    case ShaderAssetResolveCode::ArtifactInvalid: return "ArtifactInvalid";
    case ShaderAssetResolveCode::ProgramNotFound: return "ProgramNotFound";
    case ShaderAssetResolveCode::ProgramAmbiguous: return "ProgramAmbiguous";
    case ShaderAssetResolveCode::EntryNotFound: return "EntryNotFound";
    case ShaderAssetResolveCode::StageMismatch: return "StageMismatch";
    case ShaderAssetResolveCode::ProfileMismatch: return "ProfileMismatch";
    case ShaderAssetResolveCode::EngineInterfaceMismatch: return "EngineInterfaceMismatch";
    case ShaderAssetResolveCode::RootLayoutMismatch: return "RootLayoutMismatch";
    case ShaderAssetResolveCode::DeviceCapabilityMismatch: return "DeviceCapabilityMismatch";
    default: return "Unknown";
    }
}

ShaderAssetEntryLease::ShaderAssetEntryLease(std::shared_ptr<const ShaderCookedArtifact> artifact,
                                             size_t entryIndex) noexcept
    : artifact_(std::move(artifact)), entryIndex_(entryIndex) {}

ShaderAssetEntryLease::operator bool() const noexcept {
    return artifact_ != nullptr && entryIndex_ < artifact_->entries.size();
}

const ShaderCookedArtifact* ShaderAssetEntryLease::GetArtifact() const noexcept {
    return static_cast<bool>(*this) ? artifact_.get() : nullptr;
}

const ShaderCookedEntry* ShaderAssetEntryLease::GetEntry() const noexcept {
    return static_cast<bool>(*this) ? &artifact_->entries[entryIndex_] : nullptr;
}

std::span<const uint8_t> ShaderAssetEntryLease::GetDxil() const noexcept {
    const ShaderCookedEntry* entry = GetEntry();
    return entry != nullptr ? std::span<const uint8_t>(entry->dxil) : std::span<const uint8_t>{};
}

ShaderAssetResolveResult ShaderAssetResolver::ResolveByGuid(const AssetRegistry& registry, const AssetGuid& assetGuid,
                                                            const ShaderAssetEntryExpectation& entry,
                                                            const ShaderAssetContractExpectation& contract) {
    ShaderAssetResolveResult request = ValidateRequest(entry, contract);
    if (request.code != ShaderAssetResolveCode::Success) {
        return request;
    }
    if (!IsValidAssetGuid(assetGuid.value)) {
        return Failure(ShaderAssetResolveCode::InvalidRequest, "shader asset GUID is invalid");
    }

    const AssetDescriptor* base = registry.FindDescriptor(assetGuid.value);
    if (base == nullptr) {
        return Failure(ShaderAssetResolveCode::AssetNotFound, "shader asset is not registered");
    }
    if (base->type != AssetType::Shader) {
        return Failure(ShaderAssetResolveCode::AssetTypeMismatch, "registered asset is not a Shader");
    }
    const auto* descriptor = dynamic_cast<const ShaderAssetDescriptor*>(base);
    if (descriptor == nullptr) {
        return Failure(ShaderAssetResolveCode::AssetTypeMismatch, "Shader descriptor type is invalid");
    }
    return ResolveDescriptor(*descriptor, entry, contract);
}

ShaderAssetResolveResult ShaderAssetResolver::ResolveArtifact(std::shared_ptr<const ShaderCookedArtifact> artifact,
                                                              const ShaderAssetEntryExpectation& entry,
                                                              const ShaderAssetContractExpectation& contract) {
    ShaderAssetResolveResult request = ValidateRequest(entry, contract);
    if (request.code != ShaderAssetResolveCode::Success) {
        return request;
    }
    if (artifact == nullptr || !IsValidAssetGuid(artifact->sourceGuid.value)) {
        return Failure(ShaderAssetResolveCode::ArtifactUnavailable, "Shader artifact is unavailable");
    }
    ShaderAssetDescriptor descriptor{};
    descriptor.id.value = artifact->sourceGuid.value;
    descriptor.type = AssetType::Shader;
    descriptor.program = std::move(artifact);
    return ResolveDescriptor(descriptor, entry, contract);
}

ShaderAssetResolveResult ShaderAssetResolver::ResolveDescriptor(const ShaderAssetDescriptor& descriptor,
                                                                const ShaderAssetEntryExpectation& entry,
                                                                const ShaderAssetContractExpectation& contract) {
    const std::shared_ptr<const ShaderCookedArtifact>& artifact = descriptor.program;
    if (artifact == nullptr) {
        return Failure(ShaderAssetResolveCode::ArtifactUnavailable, "Shader descriptor has no cooked artifact");
    }
    if (descriptor.id.value != artifact->sourceGuid.value) {
        return Failure(ShaderAssetResolveCode::ArtifactInvalid, "Shader descriptor and artifact GUIDs differ");
    }

    std::string validationError{};
    if (!ValidateShaderCookedArtifact(*artifact, validationError)) {
        return Failure(ShaderAssetResolveCode::ArtifactInvalid, "HSHADERC validation failed: " + validationError);
    }
    if (artifact->engineInterfaceId != contract.engineInterfaceId ||
        artifact->engineInterfaceVersion != contract.engineInterfaceVersion ||
        artifact->engineInterfaceHash != contract.engineInterfaceHash) {
        return Failure(ShaderAssetResolveCode::EngineInterfaceMismatch,
                       "Shader engine-interface contract does not match");
    }
    if (artifact->rootLayoutId != contract.rootLayoutId || artifact->rootLayoutVersion != contract.rootLayoutVersion ||
        artifact->rootLayoutHash != contract.rootLayoutHash) {
        return Failure(ShaderAssetResolveCode::RootLayoutMismatch, "Shader root-layout contract does not match");
    }
    const ShaderRuntimeDeviceCapabilities& available = contract.availableDeviceCaps;
    const ShaderDeviceCaps& required = artifact->requiredCaps;
    if (available.shaderModel < required.minimumShaderModel ||
        available.rootSignatureVersion < required.minimumRootSignatureVersion ||
        (required.requiredFeatureMask & ~available.featureMask) != 0u) {
        return Failure(ShaderAssetResolveCode::DeviceCapabilityMismatch, "Shader device requirements are unavailable");
    }

    for (size_t index = 0u; index < artifact->entries.size(); ++index) {
        const ShaderCookedEntry& candidate = artifact->entries[index];
        if (candidate.entry.id != entry.entryId) {
            continue;
        }
        if (candidate.entry.stage != entry.stage) {
            return Failure(ShaderAssetResolveCode::StageMismatch, "Shader entry stage does not match");
        }
        if (candidate.entry.profile != entry.profile) {
            return Failure(ShaderAssetResolveCode::ProfileMismatch, "Shader entry profile does not match");
        }

        ShaderAssetResolveResult result{};
        result.code = ShaderAssetResolveCode::Success;
        result.lease = ShaderAssetEntryLease(artifact, index);
        return result;
    }
    return Failure(ShaderAssetResolveCode::EntryNotFound, "Shader entry ID is not present in the artifact");
}

} // namespace HIKARI::ASSETS::SHADER
