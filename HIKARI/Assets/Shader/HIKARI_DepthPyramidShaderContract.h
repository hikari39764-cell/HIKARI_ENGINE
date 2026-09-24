#pragma once

#include <string>
#include <string_view>

#include "Assets/Shader/HIKARI_ShaderContract.h"

namespace HIKARI::ASSETS::SHADER {

inline constexpr std::string_view kDepthPyramidEngineInterfaceId = "HIKARI.DepthPyramid";
inline constexpr std::string_view kDepthPyramidInterfaceDependency =
    "EngineSdk/Include/Depth/HIKARI_DepthPyramidInterface.hlsli";
inline constexpr ShaderContractIdentity kDepthPyramidContract{
    kDepthPyramidEngineInterfaceId,
    1u,
    "1ce61199638f9d29faa42c443e7951085d06199f8c0bb1f2cba67e1ab83e01c7",
    "HIKARI.DepthPyramid.Root",
    1u,
    "98c6beedfd9b25a8237518427ac18f02edeab56539ec350e645c4855a8d61d40",
    "HIKARI.DepthPyramid.Root.v1|"
    "0:Constants(b0,space0,count4,All)|"
    "1:DescriptorTable(SRV(t0,space0,count1,offset0),All)|"
    "2:DescriptorTable(UAV(u0,space0,count1,offset0),All)|"
    "Pipeline(Compute)|Flags(None)",
};

inline ShaderAssetContractExpectation MakeDepthPyramidShaderContract(
    ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
    return MakeShaderContract(kDepthPyramidContract, availableCaps);
}

inline bool ValidateDepthPyramidShaderArtifact(const ShaderCookedArtifact& artifact, const ShaderCookedEntry& entry,
                                               std::string& outDiagnostic) {
    const ShaderAssetContractExpectation expected = MakeDepthPyramidShaderContract(
        {artifact.requiredCaps.minimumShaderModel, artifact.requiredCaps.minimumRootSignatureVersion,
         artifact.requiredCaps.requiredFeatureMask});
    if (!MatchesShaderContract(artifact, expected) ||
        artifact.programId != "HIKARI.EngineShader.DepthPyramid_BuildCS" || artifact.entries.size() != 1u ||
        !MatchesEntryPoint(entry, "Main", "main", ShaderStage::Compute, "cs_6_0")) {
        outDiagnostic = "DepthPyramid artifact identity is invalid";
        return false;
    }
    constexpr uint32_t cs = ShaderStageMask(ShaderStage::Compute);
    constexpr ShaderBindingExpectation bindings[] = {{ShaderResourceBindingKind::ConstantBuffer, 0u, cs},
                                                     {ShaderResourceBindingKind::ShaderResource, 0u, cs},
                                                     {ShaderResourceBindingKind::UnorderedAccess, 0u, cs}};
    if (!DependsOnShaderInterface(artifact, kDepthPyramidInterfaceDependency, expected.engineInterfaceHash) ||
        !HasExactBindings(entry, bindings)) {
        outDiagnostic = "DepthPyramid reflection must contain exact b0/t0/u0";
        return false;
    }
    outDiagnostic.clear();
    return true;
}

} // namespace HIKARI::ASSETS::SHADER
