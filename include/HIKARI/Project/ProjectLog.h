#pragma once

#include <cstdint>
#include <string_view>

#include "HIKARI/Project/GameProjectSdk.h"

namespace HIKARI {

enum class GameProjectLogLevel : uint8_t {
    Info,
    Warning,
    Error,
};

// 引擎のログへ一行書く（編集器のログ窓とログファイルに出る）。どのスレッドからでも呼べる。
// category はモジュールや機能の名前（"BehaviorKit" など）で、行の頭に [category] として付く。
HIKARI_GAME_PROJECT_SDK_API void WriteProjectLog(GameProjectLogLevel level, std::string_view category,
                                                 std::string_view utf8Message) noexcept;

} // namespace HIKARI
