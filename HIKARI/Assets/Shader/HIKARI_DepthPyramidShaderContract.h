#pragma once

#include <string>
#include <string_view>

#include "Assets/Shader/HIKARI_ShaderAssetResolver.h"
#include "Core/Hashing/HIKARI_Sha256.h"

namespace HIKARI::ASSETS::SHADER {

    inline constexpr std::string_view kDepthPyramidEngineInterfaceId =
        "HIKARI.DepthPyramid";
    inline constexpr uint32_t kDepthPyramidEngineInterfaceVersion = 1u;
    inline constexpr std::string_view kDepthPyramidEngineInterfaceHash =
        "1ce61199638f9d29faa42c443e7951085d06199f8c0bb1f2cba67e1ab83e01c7";
    inline constexpr std::string_view kDepthPyramidInterfaceDependency =
        "EngineSdk/Include/Depth/HIKARI_DepthPyramidInterface.hlsli";
    inline constexpr std::string_view kDepthPyramidRootLayoutId =
        "HIKARI.DepthPyramid.Root";
    inline constexpr uint32_t kDepthPyramidRootLayoutVersion = 1u;
    inline constexpr std::string_view kDepthPyramidRootLayoutHash =
        "98c6beedfd9b25a8237518427ac18f02edeab56539ec350e645c4855a8d61d40";
    inline constexpr std::string_view kDepthPyramidRootLayoutCanonical =
        "HIKARI.DepthPyramid.Root.v1|"
        "0:Constants(b0,space0,count4,All)|"
        "1:DescriptorTable(SRV(t0,space0,count1,offset0),All)|"
        "2:DescriptorTable(UAV(u0,space0,count1,offset0),All)|"
        "Pipeline(Compute)|Flags(None)";

    inline ShaderAssetContractExpectation MakeDepthPyramidShaderContract(
        ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
        ShaderAssetContractExpectation contract{};
        contract.engineInterfaceId = kDepthPyramidEngineInterfaceId;
        contract.engineInterfaceVersion =
            kDepthPyramidEngineInterfaceVersion;
        contract.rootLayoutId = kDepthPyramidRootLayoutId;
        contract.rootLayoutVersion = kDepthPyramidRootLayoutVersion;
        contract.availableDeviceCaps = availableCaps;
        HASH::Sha256Digest interfaceHash{};
        HASH::Sha256Digest rootLayoutHash{};
        HASH::Sha256Digest computedRootLayoutHash{};
        if (!HASH::ParseSha256(
                kDepthPyramidEngineInterfaceHash,
                interfaceHash) ||
            !HASH::ParseSha256(
                kDepthPyramidRootLayoutHash,
                rootLayoutHash) ||
            !HASH::ComputeSha256(
                kDepthPyramidRootLayoutCanonical,
                computedRootLayoutHash) ||
            computedRootLayoutHash != rootLayoutHash) {
            return {};
        }
        contract.engineInterfaceHash = interfaceHash;
        contract.rootLayoutHash = rootLayoutHash;
        return contract;
    }

    inline bool ValidateDepthPyramidShaderArtifact(
        const ShaderCookedArtifact& artifact,
        const ShaderCookedEntry& entry,
        std::string& outDiagnostic) {
        const ShaderAssetContractExpectation expected =
            MakeDepthPyramidShaderContract({
                artifact.requiredCaps.minimumShaderModel,
                artifact.requiredCaps.minimumRootSignatureVersion,
                artifact.requiredCaps.requiredFeatureMask
            });
        if (expected.engineInterfaceHash.IsZero() ||
            expected.rootLayoutHash.IsZero() ||
            artifact.programId !=
                "HIKARI.EngineShader.DepthPyramid_BuildCS" ||
            artifact.engineInterfaceId != expected.engineInterfaceId ||
            artifact.engineInterfaceVersion !=
                expected.engineInterfaceVersion ||
            artifact.engineInterfaceHash != expected.engineInterfaceHash ||
            artifact.rootLayoutId != expected.rootLayoutId ||
            artifact.rootLayoutVersion != expected.rootLayoutVersion ||
            artifact.rootLayoutHash != expected.rootLayoutHash ||
            artifact.requiredCaps.minimumShaderModel != 60u ||
            artifact.requiredCaps.minimumRootSignatureVersion != 10u ||
            artifact.requiredCaps.requiredFeatureMask != 0u ||
            artifact.entries.size() != 1u ||
            entry.entry.id != "Main" || entry.entry.entry != "main" ||
            entry.entry.stage != ShaderStage::Compute ||
            entry.entry.profile != "cs_6_0") {
            outDiagnostic = "DepthPyramid artifact identity is invalid";
            return false;
        }
        bool ownsInterface = false;
        for (const ShaderSourceDependency& dependency :
            artifact.dependencies) {
            ownsInterface = ownsInterface ||
                (dependency.logicalPath ==
                    kDepthPyramidInterfaceDependency &&
                 dependency.contentHash == expected.engineInterfaceHash);
        }
        const uint32_t computeVisibility =
            1u << static_cast<uint32_t>(ShaderStage::Compute);
        bool hasConstants = false;
        bool hasDepth = false;
        bool hasPyramid = false;
        for (const ShaderResourceBinding& binding : entry.bindings) {
            const bool common = binding.registerIndex == 0u &&
                binding.registerSpace == 0u && binding.count == 1u &&
                binding.visibilityMask == computeVisibility;
            hasConstants = hasConstants ||
                (common && binding.kind ==
                    ShaderResourceBindingKind::ConstantBuffer);
            hasDepth = hasDepth ||
                (common && binding.kind ==
                    ShaderResourceBindingKind::ShaderResource);
            hasPyramid = hasPyramid ||
                (common && binding.kind ==
                    ShaderResourceBindingKind::UnorderedAccess);
        }
        if (!ownsInterface || entry.bindings.size() != 3u ||
            !hasConstants || !hasDepth || !hasPyramid) {
            outDiagnostic =
                "DepthPyramid reflection must contain exact b0/t0/u0";
            return false;
        }
        outDiagnostic.clear();
        return true;
    }

} // namespace HIKARI::ASSETS::SHADER
