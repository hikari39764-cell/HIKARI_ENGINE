#pragma once

#include <string>
#include <string_view>

#include <json.hpp>

namespace HIKARI::TEXT {

enum class Language : uint8_t {
    English,
    Chinese,
    Japanese,
};

bool SetLocalizationLanguage(Language language) noexcept;
Language GetLocalizationLanguage() noexcept;

// Returns the translated UI string for a key; falls back to English, then to
// the key itself when the table has no entry.
std::string Localize(std::string_view key);

// Returns a localized window title with a stable ImGui window ID.
// Example: LocalizeWindow("ui.window.inspector", "Inspector")
// produces "检查器###Inspector" so the displayed title is translated
// while docking/layout state keeps referencing "Inspector".
std::string LocalizeWindow(
    std::string_view key,
    std::string_view stableId);

// Merges an external string table (key -> text) into the active language.
// This is the extension point for JSON localization files.
void MergeLocalizationTable(
    Language language,
    const nlohmann::json& table) noexcept;

} // namespace HIKARI::TEXT
