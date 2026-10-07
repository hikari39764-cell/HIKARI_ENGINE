#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "HIKARI/Project/GameProjectModuleApi.h"

namespace HIKARI {

class IRuntimeExtension;

} // namespace HIKARI

namespace HIKARI::RUNTIME {

class GameProjectModuleLibrary {
public:
    GameProjectModuleLibrary() = default;
    ~GameProjectModuleLibrary();

    GameProjectModuleLibrary(const GameProjectModuleLibrary&) = delete;
    GameProjectModuleLibrary& operator=(const GameProjectModuleLibrary&) = delete;

    bool LoadForLaunch(bool allowMissing, std::string& outMessage);

    bool IsLoaded() const noexcept;
    std::string_view GetModuleId() const noexcept;
    // モジュールの Feature 登録を引擎内部の拡張として場面へ渡す。未読込なら nullptr。
    IRuntimeExtension* GetRuntimeExtension() const noexcept;
    bool CreateGameSession(std::string& outMessage);
    HIKARI::IGameProjectSession* GetGameSession() const noexcept;
    void DestroyGameSession() noexcept;
    const std::filesystem::path& GetModulePath() const noexcept;

private:
    // In-process module replacement is intentionally unsupported. Runtime
    // extensions and System factories borrow DLL code until app teardown;
    // project rebuild uses the process handoff workflow instead.
    bool Load(const std::filesystem::path& modulePath, std::string& outMessage);
    void Reset() noexcept;

    void* libraryHandle_ = nullptr;
    const GameProjectModuleApi* api_ = nullptr;
    void* instance_ = nullptr;
    HIKARI::IGameProjectSession* gameSession_ = nullptr;
    std::filesystem::path modulePath_{};
    std::string moduleId_{};
    std::unique_ptr<IRuntimeExtension> extension_{};
};

} // namespace HIKARI::RUNTIME
