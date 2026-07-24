#include "Core/IO/HIKARI_PathNormalization.h"

#include <system_error>

namespace HIKARI::PATHS {

    std::filesystem::path MakeAbsoluteNormalized(
        const std::filesystem::path& path) {

        std::error_code ec{};
        const std::filesystem::path absolute =
            std::filesystem::absolute(path, ec);
        return ec ? path.lexically_normal() : absolute.lexically_normal();
    }

} // namespace HIKARI::PATHS
