#pragma once

#include <filesystem>
#include <string>

namespace HIKARI::EDITOR {

std::filesystem::path ResolveExportEngineSourceRoot();

bool ResolveGameShippingHostExecutable(const std::filesystem::path& runtimeRoot, std::filesystem::path& executable,
                                       std::string& message);

bool ValidateGameShippingArtifacts(const std::filesystem::path& projectRoot, const std::filesystem::path& sourceExe,
                                   const std::filesystem::path& sourceProjectModule, std::string& outMessage);

} // namespace HIKARI::EDITOR
