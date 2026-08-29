#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace HIKARI::EDITOR {

    bool ComputeGamePackageSha256(
        std::string_view bytes,
        std::string& outHex,
        std::string* outMessage = nullptr);
    bool ComputeGamePackageFileSha256(
        const std::filesystem::path& path,
        std::string& outHex,
        std::string* outMessage = nullptr);

} // namespace HIKARI::EDITOR
