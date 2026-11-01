#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Assets/Importers/HIKARI_AssetImporter.h"
#include "RenderContracts/Shader/HIKARI_ShaderCookedArtifact.h"
#include "Core/Hashing/HIKARI_Sha256.h"
#include "Gfx/HIKARI_ShaderCookCompiler.h"

namespace HIKARI::ASSETS::IMPORTERS::INTERNAL {

bool VerifyShaderImportSourceState(const GFX::ShaderCookRequest& request,
                                   const std::vector<GFX::ShaderCookDependency>& dependencies,
                                   const std::filesystem::path& manifestPath,
                                   const HASH::Sha256Digest& expectedManifestHash, std::string& outError);

void AppendShaderProjectDependencies(const SHADER::ShaderCookedArtifact& artifact, const std::string& mainLogicalPath,
                                     AssetImportResult& result);

} // namespace HIKARI::ASSETS::IMPORTERS::INTERNAL
