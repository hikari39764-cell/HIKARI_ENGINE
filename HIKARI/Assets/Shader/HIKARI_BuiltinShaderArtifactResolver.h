#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/Shader/HIKARI_ShaderAssetResolver.h"

namespace HIKARI::ASSETS::SHADER {

struct BuiltinShaderArtifactProgram {
    std::string programId{};
    AssetGuid assetGuid{};
    std::shared_ptr<const ShaderCookedArtifact> artifact{};
};

bool LoadBuiltinShaderArtifactBundle(const std::filesystem::path& shaderRoot,
                                     std::vector<BuiltinShaderArtifactProgram>& outPrograms,
                                     std::string& outError) noexcept;

// Resolves a build-produced EngineSdk HSHADERC artifact. The generated
// index is the only runtime path authority; raw HLSL is never compiled by
// this resolver.
ShaderAssetResolveResult ResolveBuiltinShaderEntry(std::string_view programId, const ShaderAssetEntryExpectation& entry,
                                                   const ShaderAssetContractExpectation& contract) noexcept;

// RenderGeometry の詳細な成果物契約も検証する共通入口。
ShaderAssetResolveResult ResolveBuiltinRenderGeometryShaderEntry(
    std::string_view programId, const ShaderAssetEntryExpectation& entry,
    const ShaderAssetContractExpectation& contract) noexcept;

void ResetBuiltinShaderArtifactResolver() noexcept;

} // namespace HIKARI::ASSETS::SHADER
