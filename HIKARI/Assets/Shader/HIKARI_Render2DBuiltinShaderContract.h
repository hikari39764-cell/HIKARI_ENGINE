#pragma once

#include <span>
#include <string>
#include <string_view>

#include "Assets/Shader/HIKARI_ShaderContract.h"

namespace HIKARI::ASSETS::SHADER {

inline constexpr std::string_view kRender2DBuiltinEngineInterfaceId = "HIKARI.Render2D.Builtin";
inline constexpr std::string_view kRender2DBuiltinInterfaceDependency =
    "EngineSdk/Include/Render2D/HIKARI_Render2DBuiltinInterface.hlsli";
inline constexpr ShaderContractIdentity kRender2DBuiltinContract{
    kRender2DBuiltinEngineInterfaceId,
    1u,
    "4b0ba8ed3f6c38b45dcaef7b3cb5e5986f56a7d42b74de41848f320a0606bf37",
    "HIKARI.Render2D.Builtin.Root",
    1u,
    "10eec9a9b4514842837498f655e1ddfd0f1d02fccf04aece3b508f709dd1366c",
    "HIKARI.Render2D.Builtin.Root.v1|"
    "0:CBV(b0,space0,All)|"
    "1:DescriptorTable(SRV(t0,space0,count1,offset0),All)|"
    "2:CBV(b1,space0,All)|"
    "StaticSampler(s0,space0,LinearWrap,Pixel)|"
    "Pipeline(Graphics)|Flags(AllowInputAssembler)",
};

inline ShaderAssetContractExpectation MakeRender2DBuiltinShaderContract(
    ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
    return MakeShaderContract(kRender2DBuiltinContract, availableCaps);
}

inline bool ValidateRender2DBuiltinShaderArtifact(const ShaderCookedArtifact& artifact, std::string& outDiagnostic) {
    const ShaderAssetContractExpectation expected = MakeRender2DBuiltinShaderContract(
        {artifact.requiredCaps.minimumShaderModel, artifact.requiredCaps.minimumRootSignatureVersion,
         artifact.requiredCaps.requiredFeatureMask});
    if (!MatchesShaderContract(artifact, expected) || artifact.programId != "HIKARI.EngineShader.Render2D_Builtin" ||
        artifact.entries.size() != 5u) {
        outDiagnostic = "Render2D built-in artifact identity is invalid";
        return false;
    }
    using Kind = ShaderResourceBindingKind;
    constexpr uint32_t vs = ShaderStageMask(ShaderStage::Vertex);
    constexpr uint32_t ps = ShaderStageMask(ShaderStage::Pixel);
    constexpr ShaderBindingExpectation vertexBindings[] = {{Kind::ConstantBuffer, 0u, vs}};
    constexpr ShaderBindingExpectation pixelBindings[] = {{Kind::ShaderResource, 0u, ps}, {Kind::Sampler, 0u, ps}};
    constexpr ShaderBindingExpectation maskBindings[] = {
        {Kind::ConstantBuffer, 1u, ps}, {Kind::ShaderResource, 0u, ps}, {Kind::Sampler, 0u, ps}};
    const auto matches = [&](std::string_view id, ShaderStage stage, std::string_view profile,
                             std::span<const ShaderBindingExpectation> bindings) {
        for (const ShaderCookedEntry& entry : artifact.entries) {
            if (entry.entry.id == id) {
                return MatchesEntryPoint(entry, id, id, stage, profile) && HasExactBindings(entry, bindings);
            }
        }
        return false;
    };
    if (!DependsOnShaderInterface(artifact, kRender2DBuiltinInterfaceDependency, expected.engineInterfaceHash) ||
        !matches("LinePS", ShaderStage::Pixel, "ps_6_0", {}) ||
        !matches("LineVS", ShaderStage::Vertex, "vs_6_0", vertexBindings) ||
        !matches("MaskPS", ShaderStage::Pixel, "ps_6_0", maskBindings) ||
        !matches("MeshPS", ShaderStage::Pixel, "ps_6_0", pixelBindings) ||
        !matches("MeshVS", ShaderStage::Vertex, "vs_6_0", vertexBindings)) {
        outDiagnostic = "Render2D built-in reflection does not match its root layout";
        return false;
    }
    outDiagnostic.clear();
    return true;
}

} // namespace HIKARI::ASSETS::SHADER
