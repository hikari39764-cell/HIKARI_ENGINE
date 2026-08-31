#pragma once

#include <string>
#include <string_view>

#include "Assets/Shader/HIKARI_ShaderAssetResolver.h"
#include "Core/Hashing/HIKARI_Sha256.h"
#include "HIKARI/Project/RenderDispatch.h"

namespace HIKARI::ASSETS::SHADER {

    inline constexpr std::string_view kProjectDispatchInterfaceDependency =
        "EngineSdk/Project/Dispatch.hlsli";
    inline constexpr std::string_view kProjectDispatchRootLayoutCanonical =
        "HIKARI.Project.Dispatch.Root.v1|"
        "0:DescriptorTable(SRV(t0,space0,count1,offset0),"
        "UAV(u0,space0,count1,offset1),All)|"
        "1:Constants(b0,space0,count4,All)|"
        "Pipeline(Compute)|Flags(None)";

    inline ShaderAssetContractExpectation MakeProjectDispatchShaderContract(
        ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
        ShaderAssetContractExpectation contract{};
        contract.engineInterfaceId =
            kGameProjectDispatchShaderEngineInterfaceId;
        contract.engineInterfaceVersion =
            kGameProjectDispatchShaderEngineInterfaceVersion;
        contract.rootLayoutId = kGameProjectDispatchShaderRootLayoutId;
        contract.rootLayoutVersion =
            kGameProjectDispatchShaderRootLayoutVersion;
        contract.availableDeviceCaps = availableCaps;
        HASH::Sha256Digest interfaceHash{};
        HASH::Sha256Digest rootLayoutHash{};
        HASH::Sha256Digest computedRootLayoutHash{};
        if (!HASH::ParseSha256(
                kGameProjectDispatchShaderEngineInterfaceHash,
                interfaceHash) ||
            !HASH::ParseSha256(
                kGameProjectDispatchShaderRootLayoutHash,
                rootLayoutHash) ||
            !HASH::ComputeSha256(
                kProjectDispatchRootLayoutCanonical,
                computedRootLayoutHash) ||
            computedRootLayoutHash != rootLayoutHash) {
            return {};
        }
        contract.engineInterfaceHash = interfaceHash;
        contract.rootLayoutHash = rootLayoutHash;
        return contract;
    }

    inline bool ValidateProjectDispatchShaderContract(
        const ShaderCookedArtifact& artifact,
        const ShaderCookedEntry& entry,
        std::string& outDiagnostic) {
        const ShaderAssetContractExpectation expected =
            MakeProjectDispatchShaderContract({
                artifact.requiredCaps.minimumShaderModel,
                artifact.requiredCaps.minimumRootSignatureVersion,
                artifact.requiredCaps.requiredFeatureMask
            });
        if (expected.engineInterfaceHash.IsZero() ||
            expected.rootLayoutHash.IsZero() ||
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
            entry.entry.stage != ShaderStage::Compute ||
            entry.entry.profile != "cs_6_0") {
            outDiagnostic = "project Dispatch artifact identity is invalid";
            return false;
        }
        bool ownsInterface = false;
        for (const ShaderSourceDependency& dependency :
            artifact.dependencies) {
            ownsInterface = ownsInterface ||
                (dependency.logicalPath == kProjectDispatchInterfaceDependency &&
                 dependency.contentHash == expected.engineInterfaceHash);
        }
        const uint32_t computeVisibility =
            1u << static_cast<uint32_t>(ShaderStage::Compute);
        bool hasConstants = false;
        bool hasInput = false;
        bool hasOutput = false;
        for (const ShaderResourceBinding& binding : entry.bindings) {
            const bool common = binding.registerIndex == 0u &&
                binding.registerSpace == 0u && binding.count == 1u &&
                binding.visibilityMask == computeVisibility;
            hasConstants = hasConstants ||
                (common && binding.kind ==
                    ShaderResourceBindingKind::ConstantBuffer);
            hasInput = hasInput ||
                (common && binding.kind ==
                    ShaderResourceBindingKind::ShaderResource);
            hasOutput = hasOutput ||
                (common && binding.kind ==
                    ShaderResourceBindingKind::UnorderedAccess);
        }
        if (!ownsInterface || entry.bindings.size() != 3u ||
            !hasConstants || !hasInput || !hasOutput) {
            outDiagnostic =
                "project Dispatch reflection must contain exact b0/t0/u0";
            return false;
        }
        outDiagnostic.clear();
        return true;
    }

} // namespace HIKARI::ASSETS::SHADER
