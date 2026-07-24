#pragma once

#include <filesystem>

namespace HIKARI::PATHS {

    std::filesystem::path MakeAbsoluteNormalized(
        const std::filesystem::path& path);

} // namespace HIKARI::PATHS
