#pragma once

#include <string_view>

namespace HIKARI {

    enum class RuntimeHostMode {
        Game,
        Editor,
        GamePreview,
        ExportedGame,
        ExportedGameWithTools,
    };

    inline constexpr bool IsEditorHostMode(RuntimeHostMode mode) {
        return mode == RuntimeHostMode::Editor;
    }

    inline constexpr bool IsExportedGameHostMode(RuntimeHostMode mode) {
        return mode == RuntimeHostMode::ExportedGame ||
            mode == RuntimeHostMode::ExportedGameWithTools;
    }

    inline constexpr bool IsStandaloneGameHostMode(RuntimeHostMode mode) {
        return mode == RuntimeHostMode::Game ||
            mode == RuntimeHostMode::GamePreview ||
            IsExportedGameHostMode(mode);
    }

    inline constexpr bool AllowsPortableObjectTools(RuntimeHostMode mode) {
        return mode == RuntimeHostMode::ExportedGameWithTools;
    }

    inline constexpr const char* RuntimeHostModeName(RuntimeHostMode mode) {
        switch (mode) {
        case RuntimeHostMode::Game:
            return "Game";
        case RuntimeHostMode::Editor:
            return "Editor";
        case RuntimeHostMode::GamePreview:
            return "GamePreview";
        case RuntimeHostMode::ExportedGame:
            return "ExportedGame";
        case RuntimeHostMode::ExportedGameWithTools:
            return "ExportedGameWithTools";
        default:
            return "Unknown";
        }
    }

    inline constexpr bool TryParseRuntimeHostMode(std::string_view name, RuntimeHostMode& outMode) {
        if (name == "Game" || name == "game") {
            outMode = RuntimeHostMode::Game;
            return true;
        }
        if (name == "Editor" || name == "editor") {
            outMode = RuntimeHostMode::Editor;
            return true;
        }
        if (name == "GamePreview" ||
            name == "gamePreview" ||
            name == "game_preview") {
            outMode = RuntimeHostMode::GamePreview;
            return true;
        }
        if (name == "ExportedGame" || name == "exportedGame" || name == "exported_game") {
            outMode = RuntimeHostMode::ExportedGame;
            return true;
        }
        if (name == "ExportedGameWithTools" ||
            name == "exportedGameWithTools" ||
            name == "exported_game_with_tools") {
            outMode = RuntimeHostMode::ExportedGameWithTools;
            return true;
        }
        return false;
    }

    inline constexpr RuntimeHostMode DefaultRuntimeHostMode() {
#if defined(HIKARI_WITH_EDITOR)
        return RuntimeHostMode::Editor;
#else
        return RuntimeHostMode::Game;
#endif
    }

} // namespace HIKARI
