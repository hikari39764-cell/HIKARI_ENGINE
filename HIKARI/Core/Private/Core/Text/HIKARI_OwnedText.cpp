#include "Core/Text/HIKARI_OwnedText.h"

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <utility>

#include <json.hpp>

#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_Localization.h"

namespace HIKARI::TEXT {
namespace {

struct TransparentStringHash {
    using is_transparent = void;
    size_t operator()(std::string_view value) const noexcept { return std::hash<std::string_view>{}(value); }
};
using TextTable = std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>;

// 訳文を持つ言語と、その表のファイル名。
constexpr std::pair<Language, std::string_view> kTextFiles[] = {{Language::Chinese, "zh.json"},
                                                                {Language::Japanese, "ja.json"}};

struct OwnedTables {
    std::array<TextTable, 3u> byLanguage{};
};

// 界面の描画から毎フレーム引かれる。読み込みは UI の thread で、表を引くのと同じ thread。
std::map<std::string, OwnedTables, std::less<>> gTables{};
std::mutex gMissingMutex{};
std::set<std::pair<std::string, std::string>> gMissing{};

// 訳すべき字か：英字が一つも無い（数、記号、もう訳した字）なら訳さない。
bool NeedsTranslation(std::string_view text) noexcept {
    return std::any_of(text.begin(), text.end(),
                       [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); });
}

const std::string* Find(const OwnedTables& tables, Language language, std::string_view text) {
    const TextTable& table = tables.byLanguage[static_cast<size_t>(language)];
    const auto found = table.find(text);
    return found != table.end() ? &found->second : nullptr;
}

} // namespace

bool LoadOwnedTexts(std::string_view owner, const std::filesystem::path& directory, std::string& error) {
    OwnedTables tables{};
    for (const auto& [language, fileName] : kTextFiles) {
        const std::filesystem::path path = directory / fileName;
        std::error_code exists{};
        if (!std::filesystem::is_regular_file(path, exists)) {
            continue;
        }
        nlohmann::json document{};
        if (!SERIALIZATION::JSON::ReadJsonFile(path, document, &error) || !document.is_object()) {
            error =
                "text table " + path.generic_string() + " is not a JSON object" + (error.empty() ? "" : ": " + error);
            return false;
        }
        TextTable& table = tables.byLanguage[static_cast<size_t>(language)];
        for (const auto& [source, translated] : document.items()) {
            if (!translated.is_string()) {
                error = "text table " + path.generic_string() + ": the entry \"" + source + "\" is not text";
                return false;
            }
            table.emplace(source, translated.get<std::string>());
        }
    }
    gTables.insert_or_assign(std::string(owner), std::move(tables));
    return true;
}

void ClearOwnedTexts() noexcept {
    gTables.clear();
    std::scoped_lock lock(gMissingMutex);
    gMissing.clear();
}

std::string_view TranslateOwnedText(std::string_view owner, std::string_view text) {
    const Language language = GetLocalizationLanguage();
    if (language == Language::English || !NeedsTranslation(text)) {
        return text;
    }
    // 持ち主の表 → 項目の表。持ち主が分からなければ全部の表。
    if (!owner.empty()) {
        for (const std::string_view candidate : {owner, kProjectTextOwner}) {
            if (const auto tables = gTables.find(candidate); tables != gTables.end()) {
                if (const std::string* found = Find(tables->second, language, text)) {
                    return *found;
                }
            }
        }
    } else {
        for (const auto& [name, tables] : gTables) {
            if (const std::string* found = Find(tables, language, text)) {
                return *found;
            }
        }
    }
    std::scoped_lock lock(gMissingMutex);
    gMissing.emplace(std::string(owner), std::string(text));
    return text;
}

std::vector<MissingText> CopyMissingTexts() {
    std::scoped_lock lock(gMissingMutex);
    std::vector<MissingText> missing{};
    for (const auto& [owner, text] : gMissing) {
        missing.push_back({owner, text});
    }
    return missing;
}

} // namespace HIKARI::TEXT
