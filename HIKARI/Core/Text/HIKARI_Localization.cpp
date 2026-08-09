#include "Core/Text/HIKARI_Localization.h"

#include <array>
#include <mutex>
#include <unordered_map>

namespace HIKARI::TEXT {
namespace {

Language gLanguage = Language::English;

using LocalizationTable =
    std::unordered_map<std::string, std::string>;

LocalizationTable& Table(Language language) {
    static std::array<LocalizationTable, 3u> tables{};
    return tables[static_cast<size_t>(language)];
}

void EnsureBuiltInTables() {
    static std::once_flag once;
    std::call_once(once, []() {
        Table(Language::English) = {
#include "Core/Text/Localization/HIKARI_LocalizationEnglish.inl"
        };
        Table(Language::Chinese) = {
#include "Core/Text/Localization/HIKARI_LocalizationChinese.inl"
        };
        Table(Language::Japanese) = {
#include "Core/Text/Localization/HIKARI_LocalizationJapanese.inl"
        };
    });
}

} // namespace

bool SetLocalizationLanguage(Language language) noexcept {
    EnsureBuiltInTables();
    gLanguage = language;
    return true;
}

Language GetLocalizationLanguage() noexcept {
    return gLanguage;
}

std::string Localize(std::string_view key) {
    EnsureBuiltInTables();
    const auto& active = Table(gLanguage);
    const auto found = active.find(std::string(key));
    if (found != active.end()) {
        return found->second;
    }
    if (gLanguage != Language::English) {
        const auto& english = Table(Language::English);
        const auto fallback = english.find(std::string(key));
        if (fallback != english.end()) {
            return fallback->second;
        }
    }
    return std::string(key);
}

std::string LocalizeWindow(
    std::string_view key,
    std::string_view stableId) {
    return Localize(key) + "###" + std::string(stableId);
}

void MergeLocalizationTable(
    Language language,
    const nlohmann::json& table) noexcept {
    if (!table.is_object()) {
        return;
    }
    auto& target = Table(language);
    for (auto it = table.begin(); it != table.end(); ++it) {
        if (it.value().is_string()) {
            target[it.key()] = it.value().get<std::string>();
        }
    }
}

} // namespace HIKARI::TEXT
