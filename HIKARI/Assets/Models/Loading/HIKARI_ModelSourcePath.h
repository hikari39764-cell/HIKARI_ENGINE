#pragma once

#include <filesystem>
#include <string>

namespace HIKARI::ASSETS::MODELS {

    inline std::string NormalizeModelSourcePath(
        const std::filesystem::path& path) {

        return path.lexically_normal().generic_string();
    }

} // namespace HIKARI::ASSETS::MODELS
