#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// モジュールと項目が持つ界面の文字表。英文の原文がキーで、言語ごとに <directory>/zh.json、ja.json
// （一層の {"原文": "訳文"}）。英語は原文そのものなので表を持たない。編集器がモジュールを読み込む時に読み、
// 項目を閉じる時に捨てる。引擎自身の文字（Localize のキーの表）とは別。
namespace HIKARI::TEXT {

// 項目（ゲームのコード）の表の持ち主。ProjectSettings/Text に置く。
inline constexpr std::string_view kProjectTextOwner = "project";

// owner の表を読む（前の分は置き換える）。directory が無いのは表が無いだけで誤りではない。
bool LoadOwnedTexts(std::string_view owner, const std::filesystem::path& directory, std::string& error);
void ClearOwnedTexts() noexcept;

// 原文を今の言語へ：owner の表 → 項目の表 → 原文。owner が空なら全部の表から探す。数字や記号だけの文字、もう
// 英字を含まない文字はそのまま返す。返す view は表を読み直すまで有効（無ければ text そのもの）。
std::string_view TranslateOwnedText(std::string_view owner, std::string_view text);

// 検査用：英語以外の言語で引かれて表に無かった原文（持ち主ごと、重なりなし）。
struct MissingText {
    std::string owner{};
    std::string text{};
};
std::vector<MissingText> CopyMissingTexts();

} // namespace HIKARI::TEXT
