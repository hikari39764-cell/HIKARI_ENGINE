#pragma once

#include <string>
#include <string_view>

#include "Assets/Shader/HIKARI_ShaderAssetResolver.h"
#include "Core/Hashing/HIKARI_Sha256.h"

namespace HIKARI::ASSETS::SHADER {

    inline constexpr std::string_view
        kAutomaticExposureEngineInterfaceId =
            "HIKARI.AutomaticExposure";
    inline constexpr uint32_t
        kAutomaticExposureEngineInterfaceVersion = 1u;
    inline constexpr std::string_view
        kAutomaticExposureEngineInterfaceHash =
            "f57bbf2ad4c4a04a0e9b6dc20c10949394259ab11d3a0250dce0dea8e7d2a20a";
    inline constexpr std::string_view
        kAutomaticExposureInterfaceDependency =
            "EngineSdk/Include/Color/HIKARI_AutomaticExposureInterface.hlsli";
    inline constexpr std::string_view kAutomaticExposureRootLayoutId =
        "HIKARI.AutomaticExposure.Root";
    inline constexpr uint32_t kAutomaticExposureRootLayoutVersion = 1u;
    inline constexpr std::string_view kAutomaticExposureRootLayoutHash =
        "0181d502ae864f3780ca9e37222ce5846b90c27795857b5c3c3e145bba15c9df";
    inline constexpr std::string_view kAutomaticExposureRootLayoutCanonical =
        "HIKARI.AutomaticExposure.Root.v1|"
        "0:DescriptorTable(SRV(t0,space0,count1,offset0),"
        "UAV(u0,space0,count1,offset1),All)|"
        "1:Constants(b0,space0,count4,All)|"
        "Pipeline(Compute)|Flags(None)";

    inline ShaderAssetContractExpectation
        MakeAutomaticExposureShaderContract(
            ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
        ShaderAssetContractExpectation contract{};
        contract.engineInterfaceId = kAutomaticExposureEngineInterfaceId;
        contract.engineInterfaceVersion =
            kAutomaticExposureEngineInterfaceVersion;
        contract.rootLayoutId = kAutomaticExposureRootLayoutId;
        contract.rootLayoutVersion = kAutomaticExposureRootLayoutVersion;
        contract.availableDeviceCaps = availableCaps;
        HASH::Sha256Digest interfaceHash{};
        HASH::Sha256Digest rootLayoutHash{};
        HASH::Sha256Digest computedRootLayoutHash{};
        if (!HASH::ParseSha256(
                kAutomaticExposureEngineInterfaceHash,
                interfaceHash) ||
            !HASH::ParseSha256(
                kAutomaticExposureRootLayoutHash,
                rootLayoutHash) ||
            !HASH::ComputeSha256(
                kAutomaticExposureRootLayoutCanonical,
                computedRootLayoutHash) ||
            computedRootLayoutHash != rootLayoutHash) {
            return {};
        }
        contract.engineInterfaceHash = interfaceHash;
        contract.rootLayoutHash = rootLayoutHash;
        return contract;
    }

    inline bool ValidateAutomaticExposureShaderArtifact(
        const ShaderCookedArtifact& artifact,
        const ShaderCookedEntry& entry,
        std::string& outDiagnostic) {
        const ShaderAssetContractExpectation expected =
            MakeAutomaticExposureShaderContract({
                artifact.requiredCaps.minimumShaderModel,
                artifact.requiredCaps.minimumRootSignatureVersion,
                artifact.requiredCaps.requiredFeatureMask
            });
        if (expected.engineInterfaceHash.IsZero() ||
            expected.rootLayoutHash.IsZero() ||
            artifact.programId !=
                "HIKARI.EngineShader.Color_AutomaticExposureCS" ||
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
            outDiagnostic =
                "AutomaticExposure artifact identity is invalid";
            return false;
        }
        bool ownsInterface = false;
        for (const ShaderSourceDependency& dependency :
            artifact.dependencies) {
            ownsInterface = ownsInterface ||
                (dependency.logicalPath ==
                    kAutomaticExposureInterfaceDependency &&
                 dependency.contentHash == expected.engineInterfaceHash);
        }
        const uint32_t computeVisibility =
            1u << static_cast<uint32_t>(ShaderStage::Compute);
        bool hasConstants = false;
        bool hasTexture = false;
        bool hasMeasurement = false;
        for (const ShaderResourceBinding& binding : entry.bindings) {
            const bool common = binding.registerIndex == 0u &&
                binding.registerSpace == 0u && binding.count == 1u &&
                binding.visibilityMask == computeVisibility;
            hasConstants = hasConstants ||
                (common && binding.kind ==
                    ShaderResourceBindingKind::ConstantBuffer);
            hasTexture = hasTexture ||
                (common && binding.kind ==
                    ShaderResourceBindingKind::ShaderResource);
            hasMeasurement = hasMeasurement ||
                (common && binding.kind ==
                    ShaderResourceBindingKind::UnorderedAccess);
        }
        if (!ownsInterface || entry.bindings.size() != 3u ||
            !hasConstants || !hasTexture || !hasMeasurement) {
            outDiagnostic =
                "AutomaticExposure reflection must contain exact b0/t0/u0";
            return false;
        }
        outDiagnostic.clear();
        return true;
    }

} // namespace HIKARI::ASSETS::SHADER
