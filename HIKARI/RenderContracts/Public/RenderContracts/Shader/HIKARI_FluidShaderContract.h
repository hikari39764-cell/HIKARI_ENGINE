#pragma once

#include <string>
#include <string_view>

#include "RenderContracts/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "RenderContracts/Shader/HIKARI_ShaderContract.h"
#include "Core/Hashing/HIKARI_Sha256.h"

// 体積流体の解算（Render3D_FluidSimulation.hlsl）の GPU ABI。解算の全 pass が同じ根を使う。
namespace HIKARI::ASSETS::SHADER {

inline constexpr std::string_view kFluidSimulationShaderProgram = "HIKARI.EngineShader.Render3D_FluidSimulation";
inline constexpr std::string_view kFluidSimulationShaderRootCanonical =
    "HIKARI.FluidSimulation.Root.v1|0:CBV(b0,Compute)|1:RootConstants(b1,space0,8,Compute)|"
    "2..5:SRVTable(t0..t3,count1,Compute)|6:SRV(t6,Compute)|7:UAVTable(u0,count1,Compute)|"
    "8:UAVTable(u1,count1,Compute)|9:UAVTable(u4,count1,Compute)|10:UAV(u5,Compute)|"
    "StaticSampler(s0,LinearClamp,Compute)|StaticSampler(s1,LinearBorderTransparentBlack,Compute)|Flags(None)";
inline constexpr std::string_view kFluidSimulationShaderInterfaceCanonical =
    "FluidDomainCB:96;uint4 size(cells,sourceCount),float4 cell(h,invH,dt,time),motion(boundary,vorticity,"
    "buoyancy,weight),dissipation(density,temperature),up,inertia;FluidPassCB:32;uint4 passSize,"
    "passParams(color,sliceField,sliceAxis,sliceIndex|statsOffset);FluidSource:240 rows toSource[3],toDomain[3],"
    "shape,shape2,emission,velocity,field0,field1,field2,wind,guide;MAC velocity RGBA16F (n+1)^3 xyz lower face "
    "w cell temperature;density R16F n^3;pressure/rhs R32F multigrid A P=sum-kP=D h^2 coarse rhs 4*mean residual;"
    "stats raw: brick max density f32 then (maxAbsDivergence,densitySum) f32x2 per 8^3 brick";

inline ShaderAssetContractExpectation MakeFluidSimulationShaderContract(
    ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
    ShaderAssetContractExpectation contract{};
    contract.engineInterfaceId = "HIKARI.FluidSimulation";
    contract.engineInterfaceVersion = 1u;
    contract.rootLayoutId = "HIKARI.FluidSimulation.Root";
    contract.rootLayoutVersion = 1u;
    contract.availableDeviceCaps = availableCaps;
    if (!HASH::ComputeSha256(kFluidSimulationShaderInterfaceCanonical, contract.engineInterfaceHash) ||
        !HASH::ComputeSha256(kFluidSimulationShaderRootCanonical, contract.rootLayoutHash)) {
        return {};
    }
    return contract;
}

inline bool IsFluidSimulationBindingAllowed(const ShaderResourceBinding& binding) noexcept {
    using Kind = ShaderResourceBindingKind;
    if (binding.count != 1u || binding.registerSpace != 0u) {
        return false;
    }
    switch (binding.kind) {
    case Kind::ConstantBuffer: return binding.registerIndex <= 1u;
    case Kind::ShaderResource: return binding.registerIndex <= 3u || binding.registerIndex == 6u;
    case Kind::UnorderedAccess:
        return binding.registerIndex <= 1u || binding.registerIndex == 4u || binding.registerIndex == 5u;
    case Kind::Sampler: return binding.registerIndex <= 1u;
    }
    return false;
}

inline ShaderAssetResolveResult ResolveFluidSimulationShaderEntry(
    std::string entry, ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
    auto resolved =
        ResolveBuiltinShaderEntry(kFluidSimulationShaderProgram, {std::move(entry), ShaderStage::Compute, "cs_6_0"},
                                  MakeFluidSimulationShaderContract(availableCaps));
    if (resolved) {
        for (const ShaderResourceBinding& binding : resolved.lease.GetEntry()->bindings) {
            if (!IsFluidSimulationBindingAllowed(binding)) {
                return {ShaderAssetResolveCode::ArtifactInvalid,
                        {},
                        "fluid simulation shader binding exceeds its root contract"};
            }
        }
    }
    return resolved;
}

} // namespace HIKARI::ASSETS::SHADER
