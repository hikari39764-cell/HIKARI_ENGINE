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
    "HIKARI.FluidSimulation.Root.v2|0:CBV(b0,Compute)|1:RootConstants(b1,space0,8,Compute)|"
    "2..5:SRVTable(t0..t3,count1,Compute)|6:SRV(t6,Compute)|7:UAVTable(u0,count1,Compute)|"
    "8:UAVTable(u1,count1,Compute)|9:UAVTable(u4,count1,Compute)|10:UAV(u5,Compute)|11:SRV(t7,Compute)|"
    "StaticSampler(s0,LinearClamp,Compute)|StaticSampler(s1,LinearBorderTransparentBlack,Compute)|Flags(None)";
inline constexpr std::string_view kFluidSimulationShaderInterfaceCanonical =
    "FluidDomainCB:112;uint4 size(cells,sourceCount),float4 cell(h,invH,dt,time),motion(boundary,vorticity,"
    "buoyancy,weight),dissipation(density,temperature),up,inertia,uint4 interactionCount(count,activityOffset,sinkOffset,flags clear1 count2);FluidPassCB:32;uint4 passSize,"
    "passParams(color,sliceField,sliceAxis,sliceIndex|statsOffset);FluidSource:256 rows toSource[3],toDomain[3],"
    "shape,shape2,emission,velocity,field0,field1,field2,wind,guide,flow(expansion) sphere inner radius shell,shape guide advects density only (not projected);FluidInteraction:96 toLocal[3],extents(kind 0 obstacle 1 impulse 2 force 3 emit; emit y temperature),"
    "velocity(shape),push(center,strength) obstacle Brinkman after forces,directed push accel falloff^2,"
    "radial push open-boundary expansion 30S/R falloff^2 and source outflow*area/volume*mask in rhs;MAC velocity RGBA16F (n+1)^3 xyz lower face "
    "w cell temperature;density R16F n^3;pressure/rhs R32F multigrid A P=sum-kP=D h^2 coarse rhs 4*mean residual,prolong trilinear with open-boundary sign-flipped ghosts;"
    "stats raw: brick max density f32 then (maxAbsDivergence,densitySum) f32x2 per 8^3 brick then activity u32 "
    "(1 active) then sink removed f32 per interaction row (32);emit adds strength*falloff^2 density/s and activates bricks;inactive bricks skip advection,curl and forces (velocity copied,density 0),pressure solved densely";

inline ShaderAssetContractExpectation MakeFluidSimulationShaderContract(
    ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
    ShaderAssetContractExpectation contract{};
    contract.engineInterfaceId = "HIKARI.FluidSimulation";
    contract.engineInterfaceVersion = 10u;
    contract.rootLayoutId = "HIKARI.FluidSimulation.Root";
    contract.rootLayoutVersion = 2u;
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
    case Kind::ShaderResource:
        return binding.registerIndex <= 3u || binding.registerIndex == 6u || binding.registerIndex == 7u;
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

// 体積流体の描画（Render3D_FluidVolume.hlsl）：計算（ノイズ・光照体積・相機体積・烟の影の表）と全画面（步进・時間の再構成・
// 合成）が同じ根を使う。
inline constexpr std::string_view kFluidVolumeShaderProgram = "HIKARI.EngineShader.Render3D_FluidVolume";
inline constexpr std::string_view kFluidVolumeShaderRootCanonical =
    "HIKARI.FluidVolume.Root.v2|0:CBV(b0,All)|1:RootConstants(b1,space0,4,All)|2..19:SRVTable(t0..t17,count1,All)|"
    "20:UAVTable(u0,count1,All)|21:UAVTable(u1,count1,All)|StaticSampler(s0,LinearClamp)|StaticSampler(s1,LinearBorderTransparentBlack)|"
    "StaticSampler(s2,LinearWrap)|StaticSampler(s3,PointClamp)|StaticSampler(s4,ComparisonLinearBorderWhite)|"
    "Flags(None)";
inline constexpr std::string_view kFluidVolumeShaderInterfaceCanonical =
    "FluidRenderCB:1216;mat4 inverseViewProj,previousViewProj;float4 camera,forward,size(half,"
    "render),sun(direction,shadowed),sunRadiance,ambient(rgb,exposureRatio),frame(jitter,historyWeight,steps,"
    "domains);cameraVolume(near,logRange,slices,samples);mat4 lightViewProj[4];float4 splits,shadowParams,"
    "shadowSampling,shadowBias;solarTable(origin,right extent,up resolution,toSun steps);FluidDomain[4]:144 toGrid[3],cells,look,emission,optics,detail,sun;"
    "t0..t3 density,t4..t7 velocity,t8..t11 light RG16F half,t12 scene depth,t13/t14 march color RGBA16F/depth "
    "RG32F,t15 history,t16 noise R8 64^3,t17 shadow atlas;premultiplied pre-exposed scattering and "
    "transmittance;composite ONE/INV_SRC_ALPHA;camera volume log slices;u1 solar atlas RG32F cloud-solar layout "
    "tile0 near/far,tiles 1..steps far->near transmittance";

inline ShaderAssetContractExpectation MakeFluidVolumeShaderContract(
    ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
    ShaderAssetContractExpectation contract{};
    contract.engineInterfaceId = "HIKARI.FluidVolume";
    contract.engineInterfaceVersion = 2u;
    contract.rootLayoutId = "HIKARI.FluidVolume.Root";
    contract.rootLayoutVersion = 2u;
    contract.availableDeviceCaps = availableCaps;
    if (!HASH::ComputeSha256(kFluidVolumeShaderInterfaceCanonical, contract.engineInterfaceHash) ||
        !HASH::ComputeSha256(kFluidVolumeShaderRootCanonical, contract.rootLayoutHash)) {
        return {};
    }
    return contract;
}

inline bool IsFluidVolumeBindingAllowed(const ShaderResourceBinding& binding) noexcept {
    using Kind = ShaderResourceBindingKind;
    if (binding.count != 1u || binding.registerSpace != 0u) {
        return false;
    }
    switch (binding.kind) {
    case Kind::ConstantBuffer: return binding.registerIndex <= 1u;
    case Kind::ShaderResource: return binding.registerIndex <= 17u;
    case Kind::UnorderedAccess: return binding.registerIndex <= 1u;
    case Kind::Sampler: return binding.registerIndex <= 4u;
    }
    return false;
}

inline ShaderAssetResolveResult ResolveFluidVolumeShaderEntry(std::string entry, ShaderStage stage,
                                                              ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
    const char* profile = stage == ShaderStage::Compute ? "cs_6_0" : stage == ShaderStage::Vertex ? "vs_6_0" : "ps_6_0";
    auto resolved = ResolveBuiltinShaderEntry(kFluidVolumeShaderProgram, {std::move(entry), stage, profile},
                                              MakeFluidVolumeShaderContract(availableCaps));
    if (resolved) {
        for (const ShaderResourceBinding& binding : resolved.lease.GetEntry()->bindings) {
            if (!IsFluidVolumeBindingAllowed(binding)) {
                return {ShaderAssetResolveCode::ArtifactInvalid,
                        {},
                        "fluid volume shader binding exceeds its root contract"};
            }
        }
    }
    return resolved;
}

} // namespace HIKARI::ASSETS::SHADER
