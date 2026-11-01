#pragma once

#include <string>
#include <string_view>

#include "RenderContracts/Shader/HIKARI_ShaderContract.h"
#include "HIKARI/Project/RenderDispatch.h"

namespace HIKARI::ASSETS::SHADER {

inline constexpr std::string_view kProjectDispatchInterfaceDependency = "EngineSdk/Project/Dispatch.hlsli";
inline constexpr ShaderContractIdentity kProjectDispatchContract{
    kGameProjectDispatchShaderEngineInterfaceId,
    kGameProjectDispatchShaderEngineInterfaceVersion,
    kGameProjectDispatchShaderEngineInterfaceHash,
    kGameProjectDispatchShaderRootLayoutId,
    kGameProjectDispatchShaderRootLayoutVersion,
    kGameProjectDispatchShaderRootLayoutHash,
    "HIKARI.Project.Dispatch.Root.v1|"
    "0:DescriptorTable(SRV(t0,space0,count1,offset0),"
    "UAV(u0,space0,count1,offset1),All)|"
    "1:Constants(b0,space0,count4,All)|"
    "Pipeline(Compute)|Flags(None)",
};

inline ShaderAssetContractExpectation MakeProjectDispatchShaderContract(
    ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
    return MakeShaderContract(kProjectDispatchContract, availableCaps);
}

inline bool ValidateProjectDispatchShaderContract(const ShaderCookedArtifact& artifact, const ShaderCookedEntry& entry,
                                                  std::string& outDiagnostic) {
    const ShaderAssetContractExpectation expected = MakeProjectDispatchShaderContract(
        {artifact.requiredCaps.minimumShaderModel, artifact.requiredCaps.minimumRootSignatureVersion,
         artifact.requiredCaps.requiredFeatureMask});
    if (!MatchesShaderContract(artifact, expected) || entry.entry.stage != ShaderStage::Compute ||
        entry.entry.profile != "cs_6_0") {
        outDiagnostic = "project Dispatch artifact identity is invalid";
        return false;
    }
    constexpr uint32_t cs = ShaderStageMask(ShaderStage::Compute);
    constexpr ShaderBindingExpectation bindings[] = {{ShaderResourceBindingKind::ConstantBuffer, 0u, cs},
                                                     {ShaderResourceBindingKind::ShaderResource, 0u, cs},
                                                     {ShaderResourceBindingKind::UnorderedAccess, 0u, cs}};
    if (!DependsOnShaderInterface(artifact, kProjectDispatchInterfaceDependency, expected.engineInterfaceHash) ||
        !HasExactBindings(entry, bindings)) {
        outDiagnostic = "project Dispatch reflection must contain exact b0/t0/u0";
        return false;
    }
    outDiagnostic.clear();
    return true;
}

} // namespace HIKARI::ASSETS::SHADER
