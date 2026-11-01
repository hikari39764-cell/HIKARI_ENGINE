#pragma once

#include <array>
#include <string>
#include <string_view>

#include "RenderContracts/Shader/HIKARI_RenderGeometryShaderContract.h"
#include "RenderContracts/Shader/HIKARI_ShaderParameters.h"
#include "HIKARI/Project/SpriteShader.h"

namespace HIKARI::ASSETS::SHADER {

inline constexpr std::string_view kProjectSpriteShaderInterfaceDependency = "EngineSdk/Project/Sprite.hlsli";

inline ShaderAssetContractExpectation MakeProjectSpriteShaderContract(
    ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
    return MakeShaderContract({kGameProjectSpriteShaderEngineInterfaceId,
                               kGameProjectSpriteShaderEngineInterfaceVersion,
                               kGameProjectSpriteShaderEngineInterfaceHash, kGameProjectSpriteShaderRootLayoutId,
                               kGameProjectSpriteShaderRootLayoutVersion, kGameProjectSpriteShaderRootLayoutHash,
                               kWorldSpriteDrawRootLayoutCanonical},
                              availableCaps);
}

inline bool ValidateProjectSpriteShaderArtifact(const ShaderCookedArtifact& artifact, std::string& outDiagnostic) {
    const ShaderAssetContractExpectation expected = MakeProjectSpriteShaderContract(
        {artifact.requiredCaps.minimumShaderModel, artifact.requiredCaps.minimumRootSignatureVersion,
         artifact.requiredCaps.requiredFeatureMask});
    if (!MatchesShaderContract(artifact, expected) || artifact.entries.size() != 2u) {
        outDiagnostic = "project Sprite shader identity or entry count is invalid";
        return false;
    }

    HASH::Sha256Digest renderGeometryInterfaceHash{};
    HASH::Sha256Digest projectSpriteInterfaceHash{};
    if (!HASH::ParseSha256(kRenderGeometryEngineInterfaceHash, renderGeometryInterfaceHash) ||
        !HASH::ParseSha256(kGameProjectSpriteShaderEngineInterfaceHash, projectSpriteInterfaceHash) ||
        !DependsOnShaderInterface(artifact, kProjectSpriteShaderInterfaceDependency, projectSpriteInterfaceHash) ||
        !DependsOnShaderInterface(artifact, kRenderGeometryInterfaceDependency, renderGeometryInterfaceHash)) {
        outDiagnostic = "project Sprite shader is missing a current engine shader interface include";
        return false;
    }

    constexpr std::array<std::string_view, 2u> requiredEntries{
        kGameProjectSpriteColorPixelEntryId,
        kGameProjectSpriteDepthOfFieldPixelEntryId,
    };
    for (size_t index = 0u; index < artifact.entries.size(); ++index) {
        const ShaderCookedEntry& entry = artifact.entries[index];
        if (entry.entry.id != requiredEntries[index] || entry.entry.entry != requiredEntries[index] ||
            entry.entry.stage != ShaderStage::Pixel || entry.entry.profile != "ps_6_0") {
            outDiagnostic = "project Sprite shader entries must be the fixed color and DOF pixel wrappers";
            return false;
        }
        constexpr uint32_t pixelVisibility = ShaderStageMask(ShaderStage::Pixel);
        for (const ShaderResourceBinding& binding : entry.bindings) {
            if (binding.visibilityMask != pixelVisibility ||
                !IsRenderGeometryBindingAllowed(RenderGeometryRootLayout::WorldSpriteDraw, binding)) {
                outDiagnostic = "project Sprite shader reflection exceeds the WorldSprite root contract";
                return false;
            }
        }
    }

    if (artifact.parameters.size() > kGameProjectSpriteShaderParameterSlotCount ||
        !ValidateShaderParameterDefinitions(artifact.parameters, outDiagnostic)) {
        if (outDiagnostic.empty()) {
            outDiagnostic = "project Sprite shader parameter metadata exceeds sixteen slots";
        }
        return false;
    }

    outDiagnostic.clear();
    return true;
}

} // namespace HIKARI::ASSETS::SHADER
