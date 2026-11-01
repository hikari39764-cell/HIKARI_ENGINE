#pragma once

#include <array>
#include "RenderContracts/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "Core/Hashing/HIKARI_Sha256.h"

namespace HIKARI::ASSETS::SHADER {
enum class VolumetricShaderProgram : uint8_t {
    CloudNoise,
    CloudPass,
    Fog,
    AtmosphereTransmittance,
    AtmosphereMultiScattering,
    AtmosphereSkyView,
    AtmosphereAerialPerspective,
    Count
};
struct VolumetricShaderDefinition {
    const char* program;
    const char* interfaceId;
    const char* interfaceCanonical;
    const char* rootId;
    const char* rootCanonical;
};
// Scene の設定値ではなく、producer/consumer が合意する GPU ABI。
// Fog は注入側（GatherAmbientCS / InjectCS / TemporalCS / IntegrateCS）と合成側（CompositeCS）の別 root を同じ program
// bundle に含む。
inline constexpr const char* kAtmosphereLutInterface =
    "AtmosphereLutCB:128;float4 planetParams,rayleigh,mie,ozone,ozoneParams,sunDirection,sunColorRadiance,groundColor;RGB scene-linear scattering and spectral transmittance;quadratic-height endpoint LUT;isotropic multi transport;AtmosphereViewCB:b1 28 dwords (sky view observer altitude, params.y cloud shade altitude splitting clear and under-cloud air)";
inline constexpr const char* kAtmosphereAerialPerspectiveInterface =
    "AtmosphereLutCB:128;AerialPerspectiveView:b1 28 dwords mat4 inverseViewProj,float4 cameraPreExposure,float4 params(maxDistance,cloud shade altitude),float4 cloudShading;RGBA16F 32^3 pre-exposed inscatter and mean transmittance;quadratic slices to maxDistance;air below the cloud shade altitude lit by under-cloud sky";
inline constexpr const char* kAtmosphereLutRoot =
    "0:CBV(b0);1:SRVTable(t0,1);2:SRVTable(t1,1);3:UAVTable(u0,1);4:Constants(b1,28);s0:linear-clamp;All;Flags(None)";
// 体積霧の環境光の採取と注入が読む材質 texture pool の大きさ（GFX::DESCRIPTOR::kUserSrvCount）。
inline constexpr uint32_t kFogTexturePoolCount = 3968u;
inline constexpr std::array<VolumetricShaderDefinition, static_cast<size_t>(VolumetricShaderProgram::Count)> kVolumetricShaders{
    {{"HIKARI.EngineShader.Render3D_VolumetricCloudNoise", "HIKARI.CloudNoise",
      "CloudNoiseCB:16;uint3 size;uint padding;GPU histogram equalization 1024 bins u2 counts+cdf: R8 shape 128^3 6 box mips, R8 detail 32^3 5 box mips, RGBA8 weather 1024^2 region/type/cells/turret",
      "HIKARI.CloudNoise.Root",
      "0:CBV(b0);1:UAVTable(u0,1);2:UAVTable(u1,1);3:SRVTable(t0,1);4:UAV(u2);All;Flags(None)"},
     {"HIKARI.EngineShader.Render3D_VolumetricCloudPass", "HIKARI.CloudPass",
      "VolumetricCloudCB:496;mat4 inverseViewProj,previousViewProj;float4 camera,historySize,shell,typeBase,typeThickness,weather,density,shapePhase,detailPhase,weatherPhase,lightDirection,lightColor,temporal,quality,aerial,windDelta,fallbackAmbient,airRayleigh,airMie,airUnderClouds,lightning,look,wind;premultiplied pre-exposed radiance/opacity;2x2 interleaved trace into history;front/mean depth metres RG32F;t6 SceneDepth t7 AerialPerspective t8 SkyView;CloudSolarCB:64;originDescriptor,rightExtent,upResolution,directionSteps;RG32 signed intervals and cumulative T;cloud sky RGBA16F absolute premultiplied radiance/opacity in sky-view mapping",
      "HIKARI.CloudPass.Root",
      "0:CBV(b0,Pixel);1..9:SRVTable(t0..8,1,Pixel);s0:linear-wrap;s1:linear-clamp;s2:point-clamp;Flags(IA)|Compute:0CBV(b0),1CBV(b1),2..4SRVTable(t0..2),5UAVTable(u0),6SRVTable(t8),s0linear-wrap,s1linear-clamp;All;Flags(None)"},
     {"HIKARI.EngineShader.Render3D_VolumetricLighting", "HIKARI.VolumetricFog",
      "VolumetricCB:6624;mat4 inverseVP,previousVP,lightVP[4];float4 splits,cascadeParams,shadowSampling,biasScales,camera,previousCamera,fogColorDensity,distances,directionIntensity,directionalColorShadow,ambientColorIntensity,optics,volumeSize,frameParams,debugAndDepth,aerialPerspective,jitter,ambientVolumeSize,fogNoise,fogNoiseOffset;DynamicGiVolumes[3]:80;LocalLightGrid:32;LocalShadow:1040;LocalFogBoxes[32]:112,inverseBox,colorDensity,noiseEdgeScattering,noiseOffset;debugAndDepth.w:boxCount;slices log(1+d/2m);raw jittered pre-exposed scattering/extinction per slice interval;TemporalCS reprojects history at froxel centers and blends by frameParams.x;integrated scene-linear radiance/transmittance;ambient RGBA16F mean radiance;local light shadows 3x3 compare",
      "HIKARI.VolumetricFog.Root",
      "Inject:0CBV(b0),1..3SRVTable(t0..2),4UAVTable(u0),5..7SRV(t0..2,space5),8SRVTable(t7),9SRVTable(t3),10SRVTable(t10),11SRVTable(t20,3968),s0linear-clamp,s1comparison-linear-border-white;Composite:0CBV(b0),1..3SRVTable(t4..6),4UAVTable(u1),5SRVTable(t9),s0linear-clamp;All;Flags(None)"},
     {"HIKARI.EngineShader.Render3D_AtmosphereTransmittanceCS", "HIKARI.AtmosphereLut", kAtmosphereLutInterface,
      "HIKARI.AtmosphereLut.Root", kAtmosphereLutRoot},
     {"HIKARI.EngineShader.Render3D_AtmosphereMultiScatteringCS", "HIKARI.AtmosphereLut", kAtmosphereLutInterface,
      "HIKARI.AtmosphereLut.Root", kAtmosphereLutRoot},
     {"HIKARI.EngineShader.Render3D_AtmosphereSkyViewCS", "HIKARI.AtmosphereLut", kAtmosphereLutInterface,
      "HIKARI.AtmosphereLut.Root", kAtmosphereLutRoot},
     {"HIKARI.EngineShader.Render3D_AtmosphereAerialPerspectiveCS", "HIKARI.AtmosphereAerialPerspective",
      kAtmosphereAerialPerspectiveInterface, "HIKARI.AtmosphereLut.Root", kAtmosphereLutRoot}}};
inline ShaderAssetContractExpectation MakeVolumetricShaderContract(VolumetricShaderProgram program,
                                                                   ShaderRuntimeDeviceCapabilities caps) {
    const auto index = static_cast<size_t>(program);
    if (index >= kVolumetricShaders.size())
        return {};
    const auto& definition = kVolumetricShaders[index];
    ShaderAssetContractExpectation contract{};
    contract.engineInterfaceId = definition.interfaceId;
    // 大気（LUT と空気遠近）は視点定数 params.y に雲の陰の境の高度が加わって版 2。雲は定数に look・wind が加わって版 2。
    contract.engineInterfaceVersion = program == VolumetricShaderProgram::Fog                       ? 6
                                      : program >= VolumetricShaderProgram::AtmosphereTransmittance ? 2
                                      : program == VolumetricShaderProgram::CloudPass               ? 2
                                                                                                    : 1;
    contract.rootLayoutId = definition.rootId;
    contract.rootLayoutVersion = program == VolumetricShaderProgram::Fog ? 4 : 1;
    contract.availableDeviceCaps = caps;
    if (!HASH::ComputeSha256(definition.interfaceCanonical, contract.engineInterfaceHash) ||
        !HASH::ComputeSha256(definition.rootCanonical, contract.rootLayoutHash))
        return {};
    return contract;
}
inline bool IsVolumetricBindingAllowed(VolumetricShaderProgram program, std::string_view entry,
                                       const ShaderResourceBinding& binding) {
    using Kind = ShaderResourceBindingKind;
    // 環境光の採取は動的 GI の probe atlas、注入は局所光の配光とクッキーを材質 texture pool（t20、user SRV 全体）から読む。
    if (program == VolumetricShaderProgram::Fog && (entry == "GatherAmbientCS" || entry == "InjectCS") &&
        binding.kind == Kind::ShaderResource && binding.registerSpace == 0u && binding.registerIndex == 20u)
        return binding.count == kFogTexturePoolCount;
    if (static_cast<size_t>(program) >= kVolumetricShaders.size() || binding.count != 1u ||
        (program == VolumetricShaderProgram::CloudPass && entry == "FullscreenVS"))
        return false;
    if (program == VolumetricShaderProgram::Fog && entry == "InjectCS" && binding.registerSpace == 5u)
        return binding.kind == Kind::ShaderResource && binding.registerIndex < 3u;
    if (binding.registerSpace != 0u)
        return false;
    if (program == VolumetricShaderProgram::CloudPass && (entry == "SunTransmittanceCS" || entry == "TraceCloudSkyCS"))
        return (binding.kind == Kind::ConstantBuffer && binding.registerIndex < 2u) ||
               (binding.kind == Kind::ShaderResource && (binding.registerIndex < 3u || binding.registerIndex == 8u)) ||
               (binding.kind == Kind::UnorderedAccess && binding.registerIndex == 0u) ||
               (binding.kind == Kind::Sampler && binding.registerIndex < 2u);
    if (program >= VolumetricShaderProgram::AtmosphereTransmittance) {
        if (entry != "Main")
            return false;
        const bool aerialPerspective = program == VolumetricShaderProgram::AtmosphereAerialPerspective;
        if (binding.kind == Kind::ConstantBuffer)
            return binding.registerIndex == 0u ||
                   ((aerialPerspective || program == VolumetricShaderProgram::AtmosphereSkyView) &&
                    binding.registerIndex == 1u);
        if (binding.kind == Kind::UnorderedAccess)
            return binding.registerIndex == 0u;
        if (binding.kind == Kind::Sampler)
            return program != VolumetricShaderProgram::AtmosphereTransmittance && binding.registerIndex == 0u;
        return binding.kind == Kind::ShaderResource &&
               binding.registerIndex < (program == VolumetricShaderProgram::AtmosphereSkyView || aerialPerspective ? 2u
                                        : program == VolumetricShaderProgram::AtmosphereMultiScattering            ? 1u
                                                                                                        : 0u);
    }
    if (binding.kind == Kind::ConstantBuffer)
        return binding.registerIndex == 0u;
    if (program == VolumetricShaderProgram::CloudNoise)
        return (binding.kind == Kind::UnorderedAccess && binding.registerIndex < 3u) ||
               (binding.kind == Kind::ShaderResource && binding.registerIndex == 0u);
    if (program == VolumetricShaderProgram::CloudPass)
        return (binding.kind == Kind::ShaderResource && binding.registerIndex < 9u) ||
               (binding.kind == Kind::Sampler && binding.registerIndex < 3u);
    if (program != VolumetricShaderProgram::Fog)
        return false;
    if (entry == "CompositeCS") {
        if (binding.kind == Kind::ShaderResource)
            return (binding.registerIndex >= 4u && binding.registerIndex <= 6u) || binding.registerIndex == 9u;
        if (binding.kind == Kind::UnorderedAccess)
            return binding.registerIndex == 1u;
        return binding.kind == Kind::Sampler && binding.registerIndex == 0u;
    }
    if (binding.kind == Kind::UnorderedAccess)
        return binding.registerIndex == 0u;
    if (binding.kind == Kind::Sampler)
        return binding.registerIndex < (entry == "InjectCS" ? 2u : 1u);
    if (binding.kind != Kind::ShaderResource)
        return false;
    if (entry == "InjectCS")
        return binding.registerIndex < 4u || binding.registerIndex == 7u;
    if (entry == "GatherAmbientCS")
        return binding.registerIndex == 0u;
    if (entry == "TemporalCS")
        return binding.registerIndex == 2u || binding.registerIndex == 10u;
    return entry == "IntegrateCS" && binding.registerIndex == 10u;
}
inline ShaderAssetResolveResult ResolveVolumetricShaderEntry(VolumetricShaderProgram program, std::string entry,
                                                             ShaderStage stage, ShaderRuntimeDeviceCapabilities caps) {
    if (static_cast<size_t>(program) >= kVolumetricShaders.size())
        return {};
    const auto contract = MakeVolumetricShaderContract(program, caps);
    auto resolved = ResolveBuiltinShaderEntry(kVolumetricShaders[static_cast<size_t>(program)].program,
                                              {entry, stage,
                                               stage == ShaderStage::Vertex  ? "vs_6_0"
                                               : stage == ShaderStage::Pixel ? "ps_6_0"
                                                                             : "cs_6_0"},
                                              contract);
    if (resolved)
        for (const auto& binding : resolved.lease.GetEntry()->bindings)
            if (!IsVolumetricBindingAllowed(program, entry, binding))
                return {
                    ShaderAssetResolveCode::ArtifactInvalid, {}, "volumetric shader binding exceeds its root contract"};
    return resolved;
}
} // namespace HIKARI::ASSETS::SHADER
