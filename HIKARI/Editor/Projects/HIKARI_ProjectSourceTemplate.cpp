#include "Editor/Projects/HIKARI_ProjectSourceTemplate.h"
#include "HIKARI/Project/GameProjectSdk.h"

namespace HIKARI::EDITOR::PROJECT_TEMPLATE {

std::string MakeProjectCppIdentifier(std::string_view name) {
    std::string identifier{};
    identifier.reserve(name.size() + 8u);
    bool previousUnderscore = false;
    for (const unsigned char character : name) {
        const bool alphaNumeric = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                                  (character >= '0' && character <= '9');
        if (alphaNumeric) {
            identifier.push_back(static_cast<char>(character));
            previousUnderscore = false;
        } else if (!previousUnderscore && !identifier.empty()) {
            identifier.push_back('_');
            previousUnderscore = true;
        }
    }
    while (!identifier.empty() && identifier.back() == '_') {
        identifier.pop_back();
    }
    if (identifier.empty()) {
        identifier = "Game";
    }
    // C++ キーワードや予約識別子をプロジェクト名から生成しない。
    return "Project_" + identifier;
}

std::string MakeGameProjectCMake(const std::string& identifier) {
    return "cmake_minimum_required(VERSION 3.28)\n\n"
           "project(" +
           identifier +
           " LANGUAGES CXX)\n\n"
           "if(NOT WIN32 OR NOT MSVC OR "
           "NOT CMAKE_SIZEOF_VOID_P EQUAL 8)\n"
           "    message(FATAL_ERROR\n"
           "        \"HIKARI GameProjects require x64 "
           "Visual Studio 2022.\")\n"
           "endif()\n\n"
           "find_package(HIKARI " +
           std::to_string(SDK::kVersionMajor) + "." + std::to_string(SDK::kVersionMinor) + "." +
           std::to_string(SDK::kVersionPatch) +
           " CONFIG REQUIRED)\n\n"
           "hikari_add_game_project(\n"
           "    NAME \"" +
           identifier +
           "\"\n"
           "    SOURCE_DIR Source/Runtime)\n";
}

std::string MakeGameProjectCMakePresets() {
    return R"json({
  "version": 6,
  "cmakeMinimumRequired": {
    "major": 3,
    "minor": 28,
    "patch": 0
  },
  "configurePresets": [
    {
      "name": "hikari-project-base",
      "hidden": true,
      "generator": "Visual Studio 17 2022",
      "architecture": "x64",
      "cacheVariables": {
        "HIKARI_DIR": "$env{HIKARI_SDK_ROOT}/lib/cmake/HIKARI"
      }
    },
    {
      "name": "debug",
      "displayName": "HIKARI GameProject Debug",
      "inherits": "hikari-project-base",
      "binaryDir": "${sourceDir}/Library/Build/CMake/sdk-v2/debug",
      "cacheVariables": {
        "CMAKE_CONFIGURATION_TYPES": "Debug",
        "HIKARI_GAME_PROJECT_OUTPUT_ROOT": "${sourceDir}/Library/Build/debug"
      }
    },
    {
      "name": "editor-release",
      "displayName": "HIKARI GameProject Editor Release",
      "inherits": "hikari-project-base",
      "binaryDir": "${sourceDir}/Library/Build/CMake/sdk-v2/editor-release",
      "cacheVariables": {
        "CMAKE_CONFIGURATION_TYPES": "EditorRelease",
        "HIKARI_GAME_PROJECT_OUTPUT_ROOT": "${sourceDir}/Library/Build/editor-release"
      }
    },
    {
      "name": "game-shipping",
      "displayName": "HIKARI GameProject Shipping",
      "inherits": "hikari-project-base",
      "binaryDir": "${sourceDir}/Library/Build/CMake/sdk-v2/game-shipping",
      "cacheVariables": {
        "CMAKE_CONFIGURATION_TYPES": "Shipping",
        "HIKARI_GAME_PROJECT_OUTPUT_ROOT": "${sourceDir}/Library/Build/game-shipping"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "debug",
      "configurePreset": "debug",
      "configuration": "Debug",
      "targets": [ "HIKARI_GameProjectModule" ]
    },
    {
      "name": "editor-release",
      "configurePreset": "editor-release",
      "configuration": "EditorRelease",
      "targets": [ "HIKARI_GameProjectModule" ]
    },
    {
      "name": "game-shipping",
      "configurePreset": "game-shipping",
      "configuration": "Shipping",
      "targets": [ "HIKARI_GameProjectModule" ]
    }
  ]
}
)json";
}

std::string MakeGameProjectModule(const std::string& identifier) {
    return R"cpp(#include "HIKARI/Project/GameFlowViews.h"
#include "HIKARI/Project/GameProjectModule.h"
#include <array>
#include <cstdint>
#include <memory>

namespace )cpp" +
           identifier + R"cpp( {

// Play 中にシーンをまたいで保持するプロジェクト状態。
class GameSession final : public HIKARI::IGameProjectSession {
public:
    void OnGameStarted(HIKARI::WorldView&) override {
        gameplaySeconds = 0.0f;
        enteredScenes = 0;
        sceneActive = false;
    }

    void OnSceneEntered(HIKARI::WorldView&) override {
        ++enteredScenes;
        sceneActive = true;
    }
    void OnSceneExiting(HIKARI::WorldView&) override { sceneActive = false; }

    float gameplaySeconds = 0.0f;
    uint32_t enteredScenes = 0;
    bool sceneActive = false;
};

// システムは通常どおり World に Attach され、ゲーム状態は Play session から取得する。
class Game final : public HIKARI::IGameSystem {
public:
    void Update(HIKARI::WorldView& world, const HIKARI::FrameContext& frame) override {
        auto* session = dynamic_cast<GameSession*>(world.GetGameSession());
        if (session == nullptr || !session->sceneActive || frame.paused) {
            return;
        }
        session->gameplaySeconds += frame.gameDt;
    }
};

// 以下はエンジンへの登録。日常のゲーム処理は上の Game に書く。
// Register で登録した組件・系统は自動的にこの Feature の所有になる。
class GameplayFeature final : public HIKARI::IRuntimeFeature {
public:
    std::string_view GetFeatureId() const noexcept override { return ")cpp" +
           identifier + R"cpp(.Gameplay"; }
    std::string_view GetDisplayName() const noexcept override { return "Gameplay"; }
    std::string_view GetDescription() const noexcept override { return "Project game logic"; }
    std::span<const HIKARI::RuntimeSystemDefault> GetDefaultSystems() const noexcept override {
        static constexpr std::array<HIKARI::RuntimeSystemDefault, 1> systems{{{")cpp" +
           identifier + R"cpp(.Game", true, 100}}};
        return systems;
    }
    bool Register(HIKARI::RuntimeFeatureContext& context) override {
        return context.RegisterSystem<Game>({.systemId = ")cpp" +
           identifier + R"cpp(.Game", .displayName = "Game"});
    }
};

class GameProjectModule final : public HIKARI::RUNTIME::IGameProjectModule {
public:
    std::string_view GetModuleId() const noexcept override { return ")cpp" +
           identifier + R"cpp("; }
    bool RegisterRuntimeFeatures(HIKARI::RuntimeFeatureCatalog& catalog) override {
        return HIKARI::RegisterRuntimeFeature<GameplayFeature>(catalog);
    }
    std::unique_ptr<HIKARI::IGameProjectSession> CreateGameSession() override {
        return std::make_unique<GameSession>();
    }
};
} // namespace

namespace HIKARI::RUNTIME {
std::unique_ptr<IGameProjectModule> CreateGameProjectModule() {
    return std::make_unique<::)cpp" +
           identifier + R"cpp(::GameProjectModule>();
}
} // namespace HIKARI::RUNTIME
)cpp";
}

std::string MakeProjectGitAttributes() {
    return "* text=auto\n\n"
           "Assets/**/*.bin filter=lfs diff=lfs merge=lfs -text\n"
           "Assets/**/*.dds filter=lfs diff=lfs merge=lfs -text\n"
           "Assets/**/*.png filter=lfs diff=lfs merge=lfs -text\n";
}

} // namespace HIKARI::EDITOR::PROJECT_TEMPLATE
