#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"

namespace HIKARI::EDITOR {

// コンソール自身のコマンド。出力欄を消す。実行時コマンドの表には無い。
inline constexpr std::string_view kConsoleClearCommand = "clear";

// 先頭の語が人に見える編集器の操作ならその定義。無ければ nullptr（実行時コマンドとして扱う）。
const EditorOperationDefinition* FindConsoleOperation(const EditorOperationRegistry& operations, std::string_view name);

// コンソール入力の一つの語。"key=value" なら name に key が入る。
struct ConsoleToken {
    std::string name{};
    // 引用符を外した値。
    std::string text{};
    // 打った通りの値。Json 型の引数は引用符や括弧も含めて JSON として読む。
    std::string raw{};
};

// 行を空白で語に分ける。"…" と '…' と [..] {..} の中の空白では区切らない。閉じていなければ false。
// 先頭の語はコマンド名なので key=value として読まない。
bool TokenizeConsoleLine(std::string_view line, std::vector<ConsoleToken>& tokens, std::string& error);

// 各語が当たる引数の宣言。key=value は名前で、位置引数は key=value で埋まっていない宣言を宣言順に割り当てる。
// 宣言に無い key と、宣言を使い切ったあとの位置引数は nullptr。実行と補完が同じ割り当てを使う。
std::vector<const EditorOperationParameter*> AssignArgumentParameters(const EditorOperationDefinition& definition,
                                                                      std::span<const ConsoleToken> arguments);

// AssignArgumentParameters の割り当てで、宣言した型の JSON にする。
// arguments にコマンド名は含めない。宣言に無い key はそのまま文字列にし、登録表の検証に断らせる。
bool BuildOperationParameters(const EditorOperationDefinition& definition, std::span<const ConsoleToken> arguments,
                              nlohmann::json& parameters, std::string& error);

} // namespace HIKARI::EDITOR
