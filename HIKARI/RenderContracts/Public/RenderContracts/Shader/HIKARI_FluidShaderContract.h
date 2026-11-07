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
    "HIKARI.FluidSimulation.Root.v4|0:CBV(b0,Compute)|1:RootConstants(b1,space0,8,Compute)|"
    "2..5:SRVTable(t0..t3,count1,Compute)|6:SRV(t6,Compute)|7:UAVTable(u0,count1,Compute)|"
    "8:UAVTable(u1,count1,Compute)|9:UAVTable(u4,count1,Compute)|10:UAV(u5,Compute)|11:SRV(t7,Compute)|"
    "12:UAVTable(u2,count1,Compute)|13:UAV(u6,Compute)|"
    "StaticSampler(s0,LinearClamp,Compute)|StaticSampler(s1,LinearBorderTransparentBlack,Compute)|Flags(None)";
inline constexpr std::string_view kFluidSimulationShaderInterfaceCanonical =
    "FluidDomainCB:176;uint4 size(cells,sourceCount),float4 cell(h,invH,dt,time),motion(boundary,vorticity,"
    "buoyancy,weight),dissipation(density,temperature),up,inertia,uint4 interactionCount(count,activityOffset,sinkOffset,flags clear1 count2 injection4),combustion0(ignition,burnRate,fuelPerBurn,heatPerBurn),combustion1(smokePerBurn,expansionPerBurn,enabled),uint4 injectionCells(half-res xyz,0 none),float4 frameWind(relative wind m/s added to advection and source injected velocity,w occupied brick max density for optical depth 0.002 across a brick);FluidPassCB:32;uint4 passSize,"
    "passParams(color|glowCellColor,sliceField|glowIgnition,sliceAxis|glowHot,sliceIndex|statsOffset);FluidSource:304 rows toSource[3],toDomain[3],"
    "shape,shape2,emission,velocity,field0,field1,field2,wind,guide,flow(expansion),sweep(back,exposure),"
    "bounds(center,radius 0 all),color(rgb,fuelFraction) sphere inner radius shell,shape guide advects density only (not projected),"
    "mask/emission/activity use the substep-swept shape times exposure, sources skip cells outside bounds;FluidInteraction:112 toLocal[3],extents(kind 0 obstacle 1 impulse 2 force 3 emit; emit y temperature),"
    "velocity(shape),push(center,strength),sweep(emit segment,exposure|force x absorption 1/s) obstacle Brinkman after forces,directed push accel falloff^2,"
    "force absorption scales density,temperature,fuel by exp(-absorption*falloff^2*dt),sink rate 0 in all-closed domains,"
    "radial push open-boundary expansion 30S/R falloff^2 and source outflow*area/volume*mask in rhs;MAC velocity RGBA16F (n+1)^3 xyz lower face "
    "w cell temperature;density R16F n^3 or RGBA16F n^3 (x density,yzw density*color advected with density, source adds rate*color, emit white);fuel RG16F n^3 x2 or 1^3 placeholder (x fuel advected forward only,y burn rate 1/s): burn=min(burnRate*min(T-ignition,1)*dt,fuel/fuelPerBurn),fuel-=fuelPerBurn*burn,T+=heat*burn,soot+=smoke*burn,rhs expansion+=expansionPerBurn*burnRate (open boundary),occupancy max(density,fuel);pressure/rhs R32F multigrid A P=sum-kP=D h^2 coarse rhs 4*mean residual,prolong trilinear with open-boundary sign-flipped ghosts;"
    "stats raw: brick max density f32 then (maxAbsDivergence,densitySum) f32x2 per 8^3 brick then activity u32 "
    "(1 active) then removed (sink inflow+absorption) f32 per interaction row (32) then glow f32x12 per brick "
    "(sum a*(1-hotShare)*cellTint rgb,sum a,sum a*hotShare*cellTint rgb,0,sum a*grid xyz,0; a=density*saturate(T-ignition));emit adds strength*falloff^2 around its swept segment times exposure density/s and activates bricks;inactive bricks skip advection,curl and forces (velocity copied,density 0),pressure solved densely;"
    "particle injection u6 written by Render3D_ParticleSimulation: half-res int fixed x4096 channels density,pushWeight,"
    "pushWeight*velocity xyz (domain axes m/s),heatWeight,heatWeight*temperature then u32 mark per 8^3 brick; the flag-4 substep "
    "reads trilinear at cell centers (marked bricks only): density+=d white,acceleration+=(v/W-u)*sat(W)/dt,"
    "temperature+=(T/Wt-temperature)*sat(Wt),activity marks marked bricks and 26 neighbors; InjectionClearCS zeroes it";

inline ShaderAssetContractExpectation MakeFluidSimulationShaderContract(
    ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
    ShaderAssetContractExpectation contract{};
    contract.engineInterfaceId = "HIKARI.FluidSimulation";
    contract.engineInterfaceVersion = 18u;
    contract.rootLayoutId = "HIKARI.FluidSimulation.Root";
    contract.rootLayoutVersion = 4u;
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
        return binding.registerIndex <= 2u || (binding.registerIndex >= 4u && binding.registerIndex <= 6u);
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

// 体積流体の描画（Render3D_FluidVolume.hlsl）：計算（ノイズ・光照体積・相機体積・烟の影の表）と全画面（步进・合成）が
// 同じ根を使う。
inline constexpr std::string_view kFluidVolumeShaderProgram = "HIKARI.EngineShader.Render3D_FluidVolume";
inline constexpr std::string_view kFluidVolumeShaderRootCanonical =
    "HIKARI.FluidVolume.Root.v4|0:CBV(b0,All)|1:RootConstants(b1,space0,4,All)|2..18:SRVTable(t0..t16,count1,All)|"
    "19:UAVTable(u0,count1,All)|20:UAVTable(u1,count1,All)|21..24:SRV(t17..t20,All)|StaticSampler(s0,LinearClamp)|StaticSampler(s1,LinearBorderTransparentBlack)|"
    "StaticSampler(s2,LinearWrap)|StaticSampler(s3,PointClamp)|StaticSampler(s4,ComparisonLinearBorderWhite)|"
    "Flags(None)";
inline constexpr std::string_view kFluidVolumeShaderInterfaceCanonical =
    "FluidRenderCB:1200;mat4 inverseViewProj camera-relative(camera at origin);float4 camera,forward(xyz,steps),size(half,"
    "render),sun(direction,shadowed),sunRadiance,ambient(rgb,domains);cameraVolume(near,logRange,slices,samples);mat4 lightViewProj[4];float4 splits,shadowParams,"
    "shadowSampling,shadowBias;solarTable(origin,right extent,up resolution,toSun steps);FluidDomain[4]:160 toGrid[3],cells,look,emission(ignition),emissionHot(hot temperature),optics,detail,sun(w cell color);"
    "t0..t3 density(x,yzw density*color),t4..t7 velocity,t8..t11 light RGBA16F half(x sun optical depth,y sky "
    "optical depth from up cone and horizontal ring transmittance,z local occlusion),sun optical depth near 3 cells "
    "from density then light,t12 scene depth,t13/t14 march color RGBA16F/march "
    "distances RG32F (stopped opaque, opacity-weighted smoke mean),t15 noise R8 64^3,t16 shadow atlas;"
    "premultiplied pre-exposed scattering and "
    "transmittance;composite ONE/INV_SRC_ALPHA;camera volume log slices;u1 solar atlas RG32F cloud-solar layout "
    "tile0 near/far,tiles 1..steps far->near transmittance;CoveragePS full-res RG16F smoke mean distance m and "
    "opacity for the volumetric fog composite;t17..t20 solver stats per domain: u32 activity per 8^3 brick at "
    "bricks*12 bytes, march and camera volume skip bricks inactive in every domain at t (DDA to the brick exit); "
    "domain step min(span/steps,0.8 h), 512 steps+skips per piece";

inline ShaderAssetContractExpectation MakeFluidVolumeShaderContract(
    ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
    ShaderAssetContractExpectation contract{};
    contract.engineInterfaceId = "HIKARI.FluidVolume";
    contract.engineInterfaceVersion = 9u;
    contract.rootLayoutId = "HIKARI.FluidVolume.Root";
    contract.rootLayoutVersion = 4u;
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
    case Kind::ShaderResource: return binding.registerIndex <= 20u;
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
