#pragma once

#include <string_view>

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

// ランタイムホストモードを文字列に変換
inline constexpr const char* RuntimeHostModeName(RuntimeHostMode mode) {
    switch (mode) {
    case RuntimeHostMode::Game: return "Game";
    case RuntimeHostMode::Editor: return "Editor";
    case RuntimeHostMode::GamePreview: return "GamePreview";
    case RuntimeHostMode::ExportedGame: return "ExportedGame";
    default: return "Unknown";
    }
}

// 文字列からランタイムホストモードを解析（大文字小文字と表記揺れに対応）
inline constexpr bool TryParseRuntimeHostMode(std::string_view name, RuntimeHostMode& outMode) {
    if (name == "Game" || name == "game") {
        outMode = RuntimeHostMode::Game;
        return true;
    }
    if (name == "Editor" || name == "editor") {
        outMode = RuntimeHostMode::Editor;
        return true;
    }
    if (name == "GamePreview" || name == "gamePreview" || name == "game_preview") {
        outMode = RuntimeHostMode::GamePreview;
        return true;
    }
    if (name == "ExportedGame" || name == "exportedGame" || name == "exported_game") {
        outMode = RuntimeHostMode::ExportedGame;
        return true;
    }
    return false;
}

} // namespace HIKARI
