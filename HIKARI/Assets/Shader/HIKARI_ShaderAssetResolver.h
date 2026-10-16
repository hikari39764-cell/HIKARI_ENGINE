#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Shader/HIKARI_ShaderCookedArtifact.h"

namespace HIKARI {
class AssetRegistry;
struct ShaderAssetDescriptor;
} // namespace HIKARI

namespace HIKARI::ASSETS::SHADER {

enum class ShaderAssetResolveCode : uint8_t {
    Success = 0u,
    InvalidRequest,
    AssetNotFound,
    AssetTypeMismatch,
    ArtifactUnavailable,
    ArtifactInvalid,
    ProgramNotFound,
    ProgramAmbiguous,
    EntryNotFound,
    StageMismatch,
    ProfileMismatch,
    EngineInterfaceMismatch,
    RootLayoutMismatch,
    DeviceCapabilityMismatch,
};

const char* ToString(ShaderAssetResolveCode code) noexcept;

struct ShaderAssetEntryExpectation {
    std::string entryId{};
    ShaderStage stage = ShaderStage::Pixel;
    std::string profile{};
};

struct ShaderRuntimeDeviceCapabilities {
    uint32_t shaderModel = 60u;
    uint32_t rootSignatureVersion = 10u;
    uint64_t featureMask = 0u;
};

struct ShaderAssetContractExpectation {
    std::string engineInterfaceId{};
    uint32_t engineInterfaceVersion = 0u;
    HASH::Sha256Digest engineInterfaceHash{};
    std::string rootLayoutId{};
    uint32_t rootLayoutVersion = 0u;
    HASH::Sha256Digest rootLayoutHash{};
    ShaderRuntimeDeviceCapabilities availableDeviceCaps{};
};

class ShaderAssetEntryLease final {
public:
    ShaderAssetEntryLease() noexcept = default;

    explicit operator bool() const noexcept;
    const ShaderCookedArtifact* GetArtifact() const noexcept;
    const ShaderCookedEntry* GetEntry() const noexcept;
    std::span<const uint8_t> GetDxil() const noexcept;

private:
    friend class ShaderAssetResolver;

    ShaderAssetEntryLease(std::shared_ptr<const ShaderCookedArtifact> artifact, size_t entryIndex) noexcept;

    std::shared_ptr<const ShaderCookedArtifact> artifact_{};
    size_t entryIndex_ = 0u;
};

struct ShaderAssetResolveResult {
    ShaderAssetResolveCode code = ShaderAssetResolveCode::InvalidRequest;
    ShaderAssetEntryLease lease{};
    std::string diagnostic{};

    static ShaderAssetResolveResult Failure(ShaderAssetResolveCode failureCode, std::string failureDiagnostic) {
        ShaderAssetResolveResult result{};
        result.code = failureCode;
        result.diagnostic = std::move(failureDiagnostic);
        return result;
    }

    explicit operator bool() const noexcept {
        return code == ShaderAssetResolveCode::Success && static_cast<bool>(lease);
    }
};

// Stateless read-only adapter over the unique AssetRegistry owner.
// Resolve while the registry is stable on its owner thread. A completed
// lease may outlive Clear/SwapContents and may be copied to read-only
// worker consumers because it owns the immutable HSHADERC payload.
// D3D12, COM, compiler, and filesystem ownership stay outside this API.
class ShaderAssetResolver final {
public:
    static ShaderAssetResolveResult ResolveArtifact(std::shared_ptr<const ShaderCookedArtifact> artifact,
                                                    const ShaderAssetEntryExpectation& entry,
                                                    const ShaderAssetContractExpectation& contract);

    static ShaderAssetResolveResult ResolveByGuid(const AssetRegistry& registry, const AssetGuid& assetGuid,
                                                  const ShaderAssetEntryExpectation& entry,
                                                  const ShaderAssetContractExpectation& contract);

private:
    static ShaderAssetResolveResult ResolveDescriptor(const ShaderAssetDescriptor& descriptor,
                                                      const ShaderAssetEntryExpectation& entry,
                                                      const ShaderAssetContractExpectation& contract);
};

} // namespace HIKARI::ASSETS::SHADER
