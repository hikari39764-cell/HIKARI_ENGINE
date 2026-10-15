#pragma once

#include "Core/Text/HIKARI_EnumNames.h"

namespace HIKARI {

// ランタイムホストの実行モード
enum class RuntimeHostMode {
    Game,         // スタンドアロンゲーム実行
    Editor,       // エディタ環境での実行
    GamePreview,  // エディタ内ゲームプレビュー
    ExportedGame, // 出力されたゲーム実行
};

// エディタモードかどうかを判定
inline constexpr bool IsEditorHostMode(RuntimeHostMode mode) { return mode == RuntimeHostMode::Editor; }

// 出力されたゲームモードかどうかを判定
inline constexpr bool IsExportedGameHostMode(RuntimeHostMode mode) { return mode == RuntimeHostMode::ExportedGame; }

// スタンドアロンゲーム実行モード（プレビューと出力も含む）かどうかを判定
inline constexpr bool IsStandaloneGameHostMode(RuntimeHostMode mode) {
    return mode == RuntimeHostMode::Game || mode == RuntimeHostMode::GamePreview || IsExportedGameHostMode(mode);
}

// 起動設定（runtime.hostMode）に書く名前。
inline constexpr TEXT::EnumName<RuntimeHostMode> kRuntimeHostModeNames[] = {
    {RuntimeHostMode::Game, "Game"},
    {RuntimeHostMode::Editor, "Editor"},
    {RuntimeHostMode::GamePreview, "GamePreview"},
    {RuntimeHostMode::ExportedGame, "ExportedGame"},
};

inline constexpr const char* RuntimeHostModeName(RuntimeHostMode mode) {
    return TEXT::EnumToName(kRuntimeHostModeNames, mode, "Unknown");
}

} // namespace HIKARI
