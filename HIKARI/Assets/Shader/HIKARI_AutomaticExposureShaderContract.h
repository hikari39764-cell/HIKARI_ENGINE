#pragma once

#include <string>
#include <string_view>

#include "Assets/Shader/HIKARI_ShaderContract.h"

namespace HIKARI::ASSETS::SHADER {

inline constexpr std::string_view kAutomaticExposureEngineInterfaceId = "HIKARI.AutomaticExposure";
inline constexpr std::string_view kAutomaticExposureInterfaceDependency =
    "EngineSdk/Include/Color/HIKARI_AutomaticExposureInterface.hlsli";
inline constexpr ShaderContractIdentity kAutomaticExposureContract{
    kAutomaticExposureEngineInterfaceId,
    1u,
    "f57bbf2ad4c4a04a0e9b6dc20c10949394259ab11d3a0250dce0dea8e7d2a20a",
    "HIKARI.AutomaticExposure.Root",
    1u,
    "0181d502ae864f3780ca9e37222ce5846b90c27795857b5c3c3e145bba15c9df",
    "HIKARI.AutomaticExposure.Root.v1|"
    "0:DescriptorTable(SRV(t0,space0,count1,offset0),"
    "UAV(u0,space0,count1,offset1),All)|"
    "1:Constants(b0,space0,count4,All)|"
    "Pipeline(Compute)|Flags(None)",
};

inline ShaderAssetContractExpectation MakeAutomaticExposureShaderContract(
    ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
    return MakeShaderContract(kAutomaticExposureContract, availableCaps);
}

inline bool ValidateAutomaticExposureShaderArtifact(const ShaderCookedArtifact& artifact,
                                                    const ShaderCookedEntry& entry, std::string& outDiagnostic) {
    const ShaderAssetContractExpectation expected = MakeAutomaticExposureShaderContract(
        {artifact.requiredCaps.minimumShaderModel, artifact.requiredCaps.minimumRootSignatureVersion,
         artifact.requiredCaps.requiredFeatureMask});
    if (!MatchesShaderContract(artifact, expected) ||
        artifact.programId != "HIKARI.EngineShader.Color_AutomaticExposureCS" || artifact.entries.size() != 1u ||
        !MatchesEntryPoint(entry, "Main", "main", ShaderStage::Compute, "cs_6_0")) {
        outDiagnostic = "AutomaticExposure artifact identity is invalid";
        return false;
    }
    constexpr uint32_t cs = ShaderStageMask(ShaderStage::Compute);
    constexpr ShaderBindingExpectation bindings[] = {{ShaderResourceBindingKind::ConstantBuffer, 0u, cs},
                                                     {ShaderResourceBindingKind::ShaderResource, 0u, cs},
                                                     {ShaderResourceBindingKind::UnorderedAccess, 0u, cs}};
    if (!DependsOnShaderInterface(artifact, kAutomaticExposureInterfaceDependency, expected.engineInterfaceHash) ||
        !HasExactBindings(entry, bindings)) {
        outDiagnostic = "AutomaticExposure reflection must contain exact b0/t0/u0";
        return false;
    }
    outDiagnostic.clear();
    return true;
}

} // namespace HIKARI::ASSETS::SHADER
