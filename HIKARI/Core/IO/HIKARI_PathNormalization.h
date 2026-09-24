#pragma once

#include <filesystem>

namespace HIKARI::PATHS {

std::filesystem::path MakeAbsoluteNormalized(const std::filesystem::path& path);

// 正規化済みの物理パスを比較する。symlink 解決は呼出側の責務。
// Windows の既存 shader/import 規約に合わせ ASCII 大文字小文字を無視する。
bool IsCanonicalDescendant(const std::filesystem::path& root, const std::filesystem::path& file);

} // namespace HIKARI::PATHS
