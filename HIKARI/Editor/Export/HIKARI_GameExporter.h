#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include "Editor/Export/HIKARI_GameExportIdentity.h"
#include <cstdint>
#include <string>
#include <vector>

namespace HIKARI::EDITOR {

    enum class GameExportWindowMode : uint8_t {
        Windowed = 0u,
        BorderlessFullscreen,
        TopmostFullscreen,
    };

    struct GameExportResult {
        bool success = false;
        std::filesystem::path outputDirectory{};
        std::string message{};
    };

    struct GameExportSceneInfo {
        std::string guid{};
        std::string displayName{};
        std::filesystem::path sourcePath{};
        bool projectStartup = false;
    };

    struct GameExportOptions {
        std::filesystem::path outputDirectory{};
        std::optional<GameExportIdentity> identity;
        std::string startupSceneGuid{};
        std::vector<std::string> sceneGuids{};
        GameExportWindowMode windowMode =
            GameExportWindowMode::BorderlessFullscreen;
        std::function<bool(const std::string&, float)> progress{};
    };

    class GameExporter {
    public:
        static std::filesystem::path GetDefaultOutputDirectory();
        static std::vector<GameExportSceneInfo> CollectSceneAssets();
        static GameExportResult Export();
        static GameExportResult Export(const GameExportOptions& options);
    };

} // namespace HIKARI::EDITOR
