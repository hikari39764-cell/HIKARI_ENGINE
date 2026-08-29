#pragma once

#include <filesystem>
#include <string>

namespace HIKARI::EDITOR {

    bool ValidateGameShippingArtifacts(
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& sourceExe,
        const std::filesystem::path& sourceProjectModule,
        std::string& outMessage);

} // namespace HIKARI::EDITOR
