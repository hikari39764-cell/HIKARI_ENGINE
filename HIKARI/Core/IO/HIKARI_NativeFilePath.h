#pragma once

#include <filesystem>

namespace HIKARI::IO {
    // Windows の長いパスを物理 I/O 境界だけで使う。資産 GUID/論理パスには保存しない。
    inline std::filesystem::path NativeFilePath(const std::filesystem::path& path) {
#if defined(_WIN32)
        if (path.empty()) return path;
        const auto absolute = std::filesystem::absolute(path).lexically_normal().make_preferred();
        const auto& native = absolute.native();
        if (!native.starts_with(L"\\\\?\\")) {
            if (native.starts_with(L"\\\\"))
                return std::filesystem::path(L"\\\\?\\UNC\\" + native.substr(2));
            return std::filesystem::path(L"\\\\?\\" + native);
        }
#endif
        return path;
    }
}
