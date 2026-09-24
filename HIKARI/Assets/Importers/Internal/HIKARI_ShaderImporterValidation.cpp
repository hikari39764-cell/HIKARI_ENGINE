#include "Assets/Importers/Internal/HIKARI_ShaderImporterValidation.h"

#include "Assets/Shader/HIKARI_ShaderSourceManifest.h"

namespace HIKARI::ASSETS::IMPORTERS::INTERNAL {

bool VerifyShaderImportSourceState(const GFX::ShaderCookRequest& request,
                                   const std::vector<GFX::ShaderCookDependency>& dependencies,
                                   const std::filesystem::path& manifestPath,
                                   const HASH::Sha256Digest& expectedManifestHash, std::string& outError) {
    if (!GFX::VerifyShaderCookDependencies(request, dependencies, outError)) {
        return false;
    }
    SHADER::ShaderSourceManifest manifest{};
    HASH::Sha256Digest manifestHash{};
    if (!SHADER::LoadShaderSourceManifest(manifestPath, manifest, outError) ||
        !SHADER::ComputeShaderSourceManifestHash(manifest, manifestHash, outError) ||
        manifestHash != expectedManifestHash) {
        if (outError.empty()) {
            outError = "shader manifest changed during cook";
        }
        return false;
    }
    outError.clear();
    return true;
}

void AppendShaderProjectDependencies(const SHADER::ShaderCookedArtifact& artifact, const std::string& mainLogicalPath,
                                     AssetImportResult& result) {
    for (const SHADER::ShaderSourceDependency& dependency : artifact.dependencies) {
        if (dependency.logicalPath.rfind("Project/", 0u) != 0u) {
            continue;
        }
        const std::string relative = dependency.logicalPath.substr(8u);
        result.dependencies.push_back(
            {{},
             (std::filesystem::path("Assets/Shaders") / relative).lexically_normal().generic_string(),
             dependency.logicalPath == mainLogicalPath ? "ShaderSource" : "ShaderInclude"});
    }
}

} // namespace HIKARI::ASSETS::IMPORTERS::INTERNAL
