#pragma once

#include <string>
#include <string_view>

#include "Assets/Shader/HIKARI_ShaderContract.h"
#include "HIKARI/Project/RenderShader.h"

namespace HIKARI::ASSETS::SHADER {

inline constexpr std::string_view kProjectFullscreenInterfaceDependency = "EngineSdk/Project/Fullscreen.hlsli";
inline constexpr ShaderContractIdentity kProjectFullscreenContract{
    kGameProjectFullscreenShaderEngineInterfaceId,
    kGameProjectFullscreenShaderEngineInterfaceVersion,
    kGameProjectFullscreenShaderEngineInterfaceHash,
    kGameProjectFullscreenShaderRootLayoutId,
    kGameProjectFullscreenShaderRootLayoutVersion,
    kGameProjectFullscreenShaderRootLayoutHash,
    "HIKARI.Project.Fullscreen.Root.v2|"
    "0:SRVTable(t0,space0,count1,Pixel)|"
    "1:CBV(b0,space0,Pixel)|"
    "2:Constants(b1,space0,count36,Pixel)|"
    "StaticSampler(s0,space0,LinearClamp,Pixel)",
};

inline ShaderAssetContractExpectation MakeProjectFullscreenShaderContract(
    ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
    return MakeShaderContract(kProjectFullscreenContract, availableCaps);
}

// VS はバインディングなし、PS は t0/s0 と任意の b1。
inline bool ValidateProjectFullscreenShaderContract(const ShaderCookedArtifact& vertexArtifact,
                                                    const ShaderCookedEntry& vertex,
                                                    const ShaderCookedArtifact& pixelArtifact,
                                                    const ShaderCookedEntry& pixel, std::string& outDiagnostic) {
    HASH::Sha256Digest expectedInterface{};
    if (!HASH::ParseSha256(kGameProjectFullscreenShaderEngineInterfaceHash, expectedInterface) ||
        !DependsOnShaderInterface(vertexArtifact, kProjectFullscreenInterfaceDependency, expectedInterface) ||
        !DependsOnShaderInterface(pixelArtifact, kProjectFullscreenInterfaceDependency, expectedInterface)) {
        outDiagnostic = "fullscreen artifact is missing its canonical HLSL interface";
        return false;
    }
    if (!vertex.bindings.empty() || pixel.bindings.size() < 2u || pixel.bindings.size() > 3u) {
        outDiagnostic = "fullscreen reflection must contain VS:none and PS:t0/s0";
        return false;
    }
    constexpr uint32_t ps = ShaderStageMask(ShaderStage::Pixel);
    if (!HasBinding(pixel, {ShaderResourceBindingKind::ShaderResource, 0u, ps}) ||
        !HasBinding(pixel, {ShaderResourceBindingKind::Sampler, 0u, ps}) ||
        (pixel.bindings.size() == 3u && !HasBinding(pixel, {ShaderResourceBindingKind::ConstantBuffer, 1u, ps}))) {
        outDiagnostic = "fullscreen pixel reflection does not match t0/s0";
        return false;
    }
    return true;
}

} // namespace HIKARI::ASSETS::SHADER
