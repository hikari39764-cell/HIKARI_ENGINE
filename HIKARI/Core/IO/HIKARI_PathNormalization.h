#pragma once

#include <filesystem>

namespace HIKARI::PATHS {

std::filesystem::path MakeAbsoluteNormalized(const std::filesystem::path& path);

// 正規化済みの物理パスを比較する。symlink 解決は呼出側の責務。
// Windows の既存 shader/import 規約に合わせ ASCII 大文字小文字を無視する。
bool IsCanonicalDescendant(const std::filesystem::path& root, const std::filesystem::path& file);

// 両方を weakly_canonical にしてから IsCanonicalDescendant で比べる（解決できなければ false）。
bool IsContainedPath(const std::filesystem::path& root, const std::filesystem::path& path);

// ".." を含むか（相対パスが外へ出るかの字句的な判定）。
bool HasParentTraversal(const std::filesystem::path& path) noexcept;

// candidate が root の要素列で始まるか（字句比較、大文字小文字を区別）。
bool IsLexicallyWithin(const std::filesystem::path& root, const std::filesystem::path& candidate) noexcept;

} // namespace HIKARI::PATHS
