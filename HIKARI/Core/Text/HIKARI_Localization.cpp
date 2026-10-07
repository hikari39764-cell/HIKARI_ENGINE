#include "Core/Text/HIKARI_Localization.h"

#include <array>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace HIKARI::TEXT {
namespace {

Language gLanguage = Language::English;

// string_view のまま検索し、照会ごとのキー文字列確保を避ける。
struct TransparentStringHash {
    using is_transparent = void;
    size_t operator()(std::string_view value) const noexcept { return std::hash<std::string_view>{}(value); }
};
using LocalizationTable = std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>;

// 表に無いキーはキー自体を表示する。参照を返せるよう一度だけ保持する。
const std::string& InternMissingKey(std::string_view key) {
    static std::mutex mutex;
    static std::unordered_set<std::string, TransparentStringHash, std::equal_to<>> keys;
    std::scoped_lock lock(mutex);
    if (const auto found = keys.find(key); found != keys.end()) {
        return *found;
    }
    return *keys.emplace(key).first;
}

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
    if (static_cast<size_t>(language) >= 3u)
        return false;
    EnsureBuiltInTables();
    gLanguage = language;
    return true;
}

const std::string& Localize(std::string_view key) {
    EnsureBuiltInTables();
    const auto& active = Table(gLanguage);
    if (const auto found = active.find(key); found != active.end()) {
        return found->second;
    }
    if (gLanguage != Language::English) {
        const auto& english = Table(Language::English);
        if (const auto fallback = english.find(key); fallback != english.end()) {
            return fallback->second;
        }
    }
    return InternMissingKey(key);
}

std::string LocalizeWindow(std::string_view key, std::string_view stableId) {
    return Localize(key) + "###" + std::string(stableId);
}

} // namespace HIKARI::TEXT
