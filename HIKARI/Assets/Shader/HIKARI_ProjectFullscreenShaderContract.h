#pragma once

#include <string>
#include <string_view>

#include "Assets/Shader/HIKARI_ShaderAssetResolver.h"
#include "Core/Hashing/HIKARI_Sha256.h"
#include "HIKARI/Project/RenderShader.h"

namespace HIKARI::ASSETS::SHADER {

    inline constexpr std::string_view
        kProjectFullscreenInterfaceDependency =
            "EngineSdk/Project/Fullscreen.hlsli";
    inline constexpr std::string_view
        kProjectFullscreenRootLayoutCanonical =
            "HIKARI.Project.Fullscreen.Root.v1|"
            "0:SRVTable(t0,space0,count1,Pixel)|"
            "1:CBV(b0,space0,Pixel)|"
            "StaticSampler(s0,space0,LinearClamp,Pixel)";

    inline ShaderAssetContractExpectation
        MakeProjectFullscreenShaderContract(
            ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
        ShaderAssetContractExpectation contract{};
        contract.engineInterfaceId =
            kGameProjectFullscreenShaderEngineInterfaceId;
        contract.engineInterfaceVersion =
            kGameProjectFullscreenShaderEngineInterfaceVersion;
        contract.rootLayoutId = kGameProjectFullscreenShaderRootLayoutId;
        contract.rootLayoutVersion =
            kGameProjectFullscreenShaderRootLayoutVersion;
        contract.availableDeviceCaps = availableCaps;
        HASH::Sha256Digest expectedInterface{};
        HASH::Sha256Digest expectedRootLayout{};
        HASH::Sha256Digest computedRootLayout{};
        if (!HASH::ParseSha256(
            kGameProjectFullscreenShaderEngineInterfaceHash,
            expectedInterface) ||
            !HASH::ParseSha256(
            kGameProjectFullscreenShaderRootLayoutHash,
            expectedRootLayout) ||
            !HASH::ComputeSha256(
                kProjectFullscreenRootLayoutCanonical,
                computedRootLayout) ||
            computedRootLayout != expectedRootLayout) {
            return {};
        }
        contract.engineInterfaceHash = expectedInterface;
        contract.rootLayoutHash = expectedRootLayout;
        return contract;
    }

    inline bool HasProjectFullscreenInterfaceDependency(
        const ShaderCookedArtifact& artifact,
        HASH::Sha256Digest expectedHash) noexcept {
        for (const ShaderSourceDependency& dependency :
             artifact.dependencies) {
            if (dependency.logicalPath ==
                    kProjectFullscreenInterfaceDependency &&
                dependency.contentHash == expectedHash) {
                return true;
            }
        }
        return false;
    }

    inline bool ValidateProjectFullscreenShaderContract(
        const ShaderCookedArtifact& vertexArtifact,
        const ShaderCookedEntry& vertex,
        const ShaderCookedArtifact& pixelArtifact,
        const ShaderCookedEntry& pixel,
        std::string& outDiagnostic) {
        HASH::Sha256Digest expectedInterface{};
        if (!HASH::ParseSha256(
                kGameProjectFullscreenShaderEngineInterfaceHash,
                expectedInterface) ||
            !HasProjectFullscreenInterfaceDependency(
                vertexArtifact,
                expectedInterface) ||
            !HasProjectFullscreenInterfaceDependency(
                pixelArtifact,
                expectedInterface)) {
            outDiagnostic =
                "fullscreen artifact is missing its canonical HLSL interface";
            return false;
        }
        if (!vertex.bindings.empty() || pixel.bindings.size() != 2u) {
            outDiagnostic =
                "fullscreen reflection must contain VS:none and PS:t0/s0";
            return false;
        }
        const uint32_t pixelVisibility =
            1u << static_cast<uint32_t>(ShaderStage::Pixel);
        bool hasTexture = false;
        bool hasSampler = false;
        for (const ShaderResourceBinding& binding : pixel.bindings) {
            const bool common = binding.registerSpace == 0u &&
                binding.registerIndex == 0u && binding.count == 1u &&
                binding.visibilityMask == pixelVisibility;
            hasTexture = hasTexture ||
                (common && binding.kind ==
                    ShaderResourceBindingKind::ShaderResource);
            hasSampler = hasSampler ||
                (common && binding.kind ==
                    ShaderResourceBindingKind::Sampler);
        }
        if (!hasTexture || !hasSampler) {
            outDiagnostic =
                "fullscreen pixel reflection does not match t0/s0";
            return false;
        }
        return true;
    }

} // namespace HIKARI::ASSETS::SHADER
