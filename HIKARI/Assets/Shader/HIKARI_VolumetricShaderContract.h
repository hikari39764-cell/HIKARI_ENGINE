#pragma once

#include <array>
#include "Assets/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "Core/Hashing/HIKARI_Sha256.h"

namespace HIKARI::ASSETS::SHADER {
enum class VolumetricShaderProgram : uint8_t {
    CloudNoise,
    CloudPass,
    Fog,
    AtmosphereTransmittance,
    AtmosphereMultiScattering,
    AtmosphereSkyView,
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
// Fog は injection/composite の別 root を同じ program bundle に含む。
inline constexpr const char* kAtmosphereLutInterface =
    "AtmosphereLutCB:128;float4 planetParams,rayleigh,mie,ozone,ozoneParams,sunDirection,sunColorRadiance,groundColor;RGB scene-linear scattering and spectral transmittance;quadratic-height endpoint LUT;isotropic multi transport";
inline constexpr const char* kAtmosphereLutRoot =
    "0:CBV(b0);1:SRVTable(t0,1);2:SRVTable(t1,1);3:UAVTable(u0,1);s0:linear-clamp;All;Flags(None)";
inline constexpr std::array<VolumetricShaderDefinition, static_cast<size_t>(VolumetricShaderProgram::Count)> kVolumetricShaders{
    {{"HIKARI.EngineShader.Render3D_StylizedCloudNoise", "HIKARI.CloudNoise",
      "CloudNoiseCB:16;uint3 size;uint kind;RGBA16F shape/detail/weather", "HIKARI.CloudNoise.Root",
      "0:CBV(b0);1:UAVTable(u0,1);2:UAVTable(u1,1);All;Flags(None)"},
     {"HIKARI.EngineShader.Render3D_StylizedCloudPass", "HIKARI.CloudPass",
      "CloudPassCB:368;mat4 inverseViewProj,previousViewProj;float4 cameraTime,previousCameraDelta,traceSize,fullSize,layer,shape,motion,lighting,sunDirectionIntensity,brightColor,shadowColor,temporal,quality,ambientLighting,sunColorAnisotropy;premultiplied radiance/opacity;depth metres;SceneDepth t7 shared projection;CloudSolarCB:64;originDescriptor,rightExtent,upResolution,directionSteps;RG32 signed intervals and cumulative T",
      "HIKARI.CloudPass.Root",
      "0:CBV(b0,Pixel);1..8:SRVTable(t0..7,1,Pixel);s0:linear-wrap;s1:linear-clamp;s2:point-clamp;Flags(IA)|Sun:0CBV(b0),1CBV(b1),2..4SRVTable(t0..2),5UAVTable(u0),s0linear-wrap;All;Flags(None)"},
     {"HIKARI.EngineShader.Render3D_VolumetricLighting", "HIKARI.VolumetricFog",
      "VolumetricCB:5344;mat4 inverseVP,previousVP,lightVP[4];float4 splits,cascadeParams,shadowSampling,biasScales,camera,previousCamera,fogColorDensity,distances,directionIntensity,directionalColorShadow,ambientColorIntensity,optics,volumeSize,frameParams,debugAndDepth;LocalLightGrid:32;LocalShadow:1040;CloudSolar:64;LocalFogBoxes[32]:112,inverseBox,colorDensity,noiseEdgeScattering,noiseOffset;debugAndDepth.w:boxCount;scene-linear radiance/transmittance",
      "HIKARI.VolumetricFog.Root",
      "Inject:0CBV(b0),1..3SRVTable(t0..2),4UAVTable(u0),5..7SRV(t0..2,space5),8SRVTable(t7),9SRVTable(t8),s0linear-clamp,s1comparison-linear-border-white;Composite:0CBV(b0),1..3SRVTable(t4..6),4UAVTable(u1),s0linear-clamp;All;Flags(None)"},
     {"HIKARI.EngineShader.Render3D_AtmosphereTransmittanceCS", "HIKARI.AtmosphereLut", kAtmosphereLutInterface,
      "HIKARI.AtmosphereLut.Root", kAtmosphereLutRoot},
     {"HIKARI.EngineShader.Render3D_AtmosphereMultiScatteringCS", "HIKARI.AtmosphereLut", kAtmosphereLutInterface,
      "HIKARI.AtmosphereLut.Root", kAtmosphereLutRoot},
     {"HIKARI.EngineShader.Render3D_AtmosphereSkyViewCS", "HIKARI.AtmosphereLut", kAtmosphereLutInterface,
      "HIKARI.AtmosphereLut.Root", kAtmosphereLutRoot}}};
inline ShaderAssetContractExpectation MakeVolumetricShaderContract(VolumetricShaderProgram program,
                                                                   ShaderRuntimeDeviceCapabilities caps) {
    const auto index = static_cast<size_t>(program);
    if (index >= kVolumetricShaders.size())
        return {};
    const auto& definition = kVolumetricShaders[index];
    ShaderAssetContractExpectation contract{};
    contract.engineInterfaceId = definition.interfaceId;
    contract.engineInterfaceVersion = program == VolumetricShaderProgram::Fog ? 2 : 1;
    contract.rootLayoutId = definition.rootId;
    contract.rootLayoutVersion = 1;
    contract.availableDeviceCaps = caps;
    if (!HASH::ComputeSha256(definition.interfaceCanonical, contract.engineInterfaceHash) ||
        !HASH::ComputeSha256(definition.rootCanonical, contract.rootLayoutHash))
        return {};
    return contract;
}
inline bool IsVolumetricBindingAllowed(VolumetricShaderProgram program, std::string_view entry,
                                       const ShaderResourceBinding& binding) {
    using Kind = ShaderResourceBindingKind;
    if (static_cast<size_t>(program) >= kVolumetricShaders.size() || binding.count != 1u ||
        (program == VolumetricShaderProgram::CloudPass && entry == "FullscreenVS"))
        return false;
    if (program == VolumetricShaderProgram::Fog && entry == "InjectAndIntegrateCS" && binding.registerSpace == 5u)
        return binding.kind == Kind::ShaderResource && binding.registerIndex < 3u;
    if (binding.registerSpace != 0u)
        return false;
    if (program == VolumetricShaderProgram::CloudPass && entry == "SunTransmittanceCS")
        return (binding.kind == Kind::ConstantBuffer && binding.registerIndex < 2u) ||
               (binding.kind == Kind::ShaderResource && binding.registerIndex < 3u) ||
               (binding.kind == Kind::UnorderedAccess && binding.registerIndex == 0u) ||
               (binding.kind == Kind::Sampler && binding.registerIndex == 0u);
    if (program >= VolumetricShaderProgram::AtmosphereTransmittance) {
        if (entry != "Main")
            return false;
        if (binding.kind == Kind::ConstantBuffer || binding.kind == Kind::UnorderedAccess)
            return binding.registerIndex == 0u;
        if (binding.kind == Kind::Sampler)
            return program != VolumetricShaderProgram::AtmosphereTransmittance && binding.registerIndex == 0u;
        return binding.kind == Kind::ShaderResource &&
               binding.registerIndex < (program == VolumetricShaderProgram::AtmosphereSkyView           ? 2u
                                        : program == VolumetricShaderProgram::AtmosphereMultiScattering ? 1u
                                                                                                        : 0u);
    }
    if (binding.kind == Kind::ConstantBuffer)
        return binding.registerIndex == 0u;
    if (program == VolumetricShaderProgram::CloudNoise)
        return binding.kind == Kind::UnorderedAccess && binding.registerIndex < 2u;
    if (program == VolumetricShaderProgram::CloudPass)
        return (binding.kind == Kind::ShaderResource && binding.registerIndex < 8u) ||
               (binding.kind == Kind::Sampler && binding.registerIndex < 3u);
    if (program != VolumetricShaderProgram::Fog)
        return false;
    const bool inject = entry == "InjectAndIntegrateCS";
    if (binding.kind == Kind::ShaderResource)
        return inject ? binding.registerIndex < 3u || binding.registerIndex == 7u || binding.registerIndex == 8u
                      : binding.registerIndex >= 4u && binding.registerIndex <= 6u;
    if (binding.kind == Kind::UnorderedAccess)
        return binding.registerIndex == (inject ? 0u : 1u);
    return binding.kind == Kind::Sampler && binding.registerIndex < (inject ? 2u : 1u);
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
