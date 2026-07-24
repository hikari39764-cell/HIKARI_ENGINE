#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace HIKARI::EDITOR {

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
        std::string startupSceneGuid{};
        std::vector<std::string> sceneGuids{};
    };

    class GameExporter {
    public:
        static std::filesystem::path GetDefaultOutputDirectory();
        static std::vector<GameExportSceneInfo> CollectSceneAssets();
        static GameExportResult Export();
        static GameExportResult Export(const GameExportOptions& options);
    };

} // namespace HIKARI::EDITOR
