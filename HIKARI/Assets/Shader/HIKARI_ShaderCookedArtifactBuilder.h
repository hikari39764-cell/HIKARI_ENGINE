#pragma once

#include <string>

#include "Core/HIKARI_AssetGuid.h"
#include "Assets/Shader/HIKARI_ShaderCookedArtifact.h"
#include "Assets/Shader/HIKARI_ShaderSourceManifest.h"
#include "Gfx/HIKARI_ShaderCookCompiler.h"

namespace HIKARI::ASSETS::SHADER {

bool BuildShaderCookRequest(const ShaderSourceManifest& manifest, ShaderIncludeRoot primarySourceRoot,
                            const std::filesystem::path& projectShaderRoot,
                            const std::filesystem::path& engineSdkShaderRoot, GFX::ShaderCookRequest& outRequest,
                            std::string& outError);

// Single artifact assembly path shared by project import and built-in
// EngineSdk cooking. The compiler output is admitted only when every
// source, toolchain, interface, root-layout and reflection identity is
// consistent with the manifest.
bool BuildShaderCookedArtifact(const AssetGuid& sourceGuid, const ShaderSourceManifest& manifest,
                               const HASH::Sha256Digest& manifestHash, ShaderIncludeRoot primarySourceRoot,
                               const GFX::ShaderCookOutput& compiled, ShaderCookedArtifact& outArtifact,
                               std::string& outError);

} // namespace HIKARI::ASSETS::SHADER
