#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace HIKARI::TEXT {

enum class Language : uint8_t {
    English,
    Chinese,
    Japanese,
};

bool SetLocalizationLanguage(Language language) noexcept;

// Returns the translated UI string for a key; falls back to English, then to
// the key itself when the table has no entry. 表は起動後に変化しないため、
// 返す参照はプロセス終了まで有効で、毎フレームの呼び出しでも文字列を確保しない。
const std::string& Localize(std::string_view key);

// Returns a localized window title with a stable ImGui window ID.
// Example: LocalizeWindow("ui.window.inspector", "Inspector")
// produces "检查器###Inspector" so the displayed title is translated
// while docking/layout state keeps referencing "Inspector".
std::string LocalizeWindow(std::string_view key, std::string_view stableId);

} // namespace HIKARI::TEXT
