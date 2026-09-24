#pragma once

#include <string>
#include <string_view>

#include "Assets/Shader/HIKARI_ShaderContract.h"

namespace HIKARI::ASSETS::SHADER {

inline constexpr std::string_view kPostEffectEngineInterfaceId = "HIKARI.PostEffect.Common";
inline constexpr std::string_view kPostEffectInterfaceDependency =
    "EngineSdk/Include/Post/HIKARI_PostEffectInterface.hlsli";
inline constexpr ShaderContractIdentity kPostEffectContract{
    kPostEffectEngineInterfaceId,
    2u,
    "3210d64ed24339581f1173a98ba739837860adae27388299528ccc572bae6ee2",
    "HIKARI.PostQuad.Root",
    3u,
    "293c005f5adefecd38ef51a4ac9ecb2463c573970cd8f4b09632f7593de0d1ad",
    "HIKARI.PostQuad.Root.v3|"
    "0:SRVTable(t0,space0,count1,Pixel)|"
    "1:CBV(b0,space0,Pixel)|"
    "2:Constants(b1,space0,count36,Pixel)|"
    "3:SRVTable(t1,space0,count2,Pixel)|"
    "StaticSampler(s0,space0,LinearClamp,Pixel)|"
    "Flags(AllowInputAssembler)",
};

inline ShaderAssetContractExpectation MakePostEffectShaderContract(
    ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
    return MakeShaderContract(kPostEffectContract, availableCaps);
}

// 各 entry は b0/t0/s0 を必須とし、b1（読みやすさ定数）と t1/t2（補助テクスチャ）を任意で持てる。
inline bool ValidatePostEffectShaderArtifact(const ShaderCookedArtifact& artifact, std::string& outDiagnostic) {
    const ShaderAssetContractExpectation expected = MakePostEffectShaderContract(
        {artifact.requiredCaps.minimumShaderModel, artifact.requiredCaps.minimumRootSignatureVersion,
         artifact.requiredCaps.requiredFeatureMask});
    if (!MatchesShaderContract(artifact, expected) || artifact.entries.empty()) {
        outDiagnostic = "PostEffect artifact identity is invalid";
        return false;
    }
    if (!DependsOnShaderInterface(artifact, kPostEffectInterfaceDependency, expected.engineInterfaceHash)) {
        outDiagnostic = "PostEffect artifact is missing its canonical interface";
        return false;
    }
    constexpr uint32_t ps = ShaderStageMask(ShaderStage::Pixel);
    using Kind = ShaderResourceBindingKind;
    for (const ShaderCookedEntry& entry : artifact.entries) {
        uint32_t auxiliaryMask = 0u;
        uint32_t auxiliaryTextures = 0u;
        for (uint32_t reg : {1u, 2u}) {
            for (const ShaderResourceBinding& binding : entry.bindings) {
                if (!MatchesBinding(binding, {Kind::ShaderResource, reg, ps})) {
                    continue;
                }
                if ((auxiliaryMask & (1u << reg)) != 0u) {
                    outDiagnostic = "Duplicate auxiliary post texture";
                    return false;
                }
                auxiliaryMask |= 1u << reg;
                ++auxiliaryTextures;
            }
        }
        const bool hasReadability = HasBinding(entry, {Kind::ConstantBuffer, 1u, ps});
        if (entry.entry.stage != ShaderStage::Pixel || entry.entry.profile != "ps_6_0" ||
            entry.bindings.size() != (hasReadability ? 4u : 3u) + auxiliaryTextures ||
            !HasBinding(entry, {Kind::ConstantBuffer, 0u, ps}) || !HasBinding(entry, {Kind::ShaderResource, 0u, ps}) ||
            !HasBinding(entry, {Kind::Sampler, 0u, ps})) {
            outDiagnostic = "PostEffect requires b0, t0, s0 with optional b1 and t1/t2";
            return false;
        }
    }
    outDiagnostic.clear();
    return true;
}

} // namespace HIKARI::ASSETS::SHADER
