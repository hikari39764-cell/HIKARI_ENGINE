#include "Core/Text/HIKARI_Localization.h"

#include <array>
#include <functional>
#include <mutex>
#include <span>
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

// 表は読み取り専用データに置き、初回に一度だけ写す。std::string の初期化子リストは数千件分が
// スタックに積まれ、主スレッドのスタックを使い切る。
struct TableEntry {
    std::string_view key;
    std::string_view text;
};
constexpr TableEntry kEnglishTable[] = {
#include "Core/Text/Localization/HIKARI_LocalizationEnglish.inl"
};
constexpr TableEntry kChineseTable[] = {
#include "Core/Text/Localization/HIKARI_LocalizationChinese.inl"
};
constexpr TableEntry kJapaneseTable[] = {
#include "Core/Text/Localization/HIKARI_LocalizationJapanese.inl"
};

void Fill(LocalizationTable& table, std::span<const TableEntry> entries) {
    table.reserve(entries.size());
    for (const TableEntry& entry : entries) {
        table.emplace(entry.key, entry.text);
    }
}

void EnsureBuiltInTables() {
    static std::once_flag once;
    std::call_once(once, []() {
        Fill(Table(Language::English), kEnglishTable);
        Fill(Table(Language::Chinese), kChineseTable);
        Fill(Table(Language::Japanese), kJapaneseTable);
    });
}

// 現在言語→英語の順に引く。無ければ nullptr。
const std::string* FindTranslation(std::string_view key) {
    EnsureBuiltInTables();
    const auto& active = Table(gLanguage);
    if (const auto found = active.find(key); found != active.end()) {
        return &found->second;
    }
    if (gLanguage != Language::English) {
        const auto& english = Table(Language::English);
        if (const auto fallback = english.find(key); fallback != english.end()) {
            return &fallback->second;
        }
    }
    return nullptr;
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
    const std::string* translated = FindTranslation(key);
    return translated != nullptr ? *translated : InternMissingKey(key);
}

std::string LocalizeWindow(std::string_view key, std::string_view stableId) {
    return Localize(key) + "###" + std::string(stableId);
}

std::string LocalizeSourceText(std::string_view prefix, std::string_view text) {
    std::string key;
    key.reserve(prefix.size() + text.size());
    key.append(prefix).append(text);
    // 動的な文も来るので、表に無いキーは Localize と違って保持しない。
    const std::string* translated = FindTranslation(key);
    return translated != nullptr ? *translated : std::string(text);
}

} // namespace HIKARI::TEXT
