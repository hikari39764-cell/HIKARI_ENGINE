#pragma once

#include <filesystem>
#include <string>

namespace HIKARI {
struct AssetRecord;
}

namespace HIKARI::ASSETS::SHADER {
struct ShaderCookedArtifact;
}

namespace HIKARI::ASSETS::SHADER::INTERNAL {

bool ValidateContentAddressedShaderArtifact(const std::filesystem::path& artifactPath, std::string& outError);

bool ValidateShaderAssetRecordAuthority(const AssetRecord& record, std::string& outError);

bool ValidateShaderAuthoringArtifactOwnership(const AssetRecord& record, const std::filesystem::path& projectRoot,
                                              const std::filesystem::path& artifactRelativePath, std::string& outError);

bool ValidateShaderArtifactAgainstAuthoringSources(const AssetRecord& record, const ShaderCookedArtifact& artifact,
                                                   const std::filesystem::path& projectRoot,
                                                   const std::filesystem::path& engineShaderRoot,
                                                   std::string& outError);

} // namespace HIKARI::ASSETS::SHADER::INTERNAL
