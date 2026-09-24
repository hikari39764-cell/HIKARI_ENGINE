#pragma once

#include <filesystem>
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
    size_t GetRuntimeExtensionCount() const noexcept;
    IRuntimeExtension* GetRuntimeExtension(size_t index) const noexcept;
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
    std::filesystem::path modulePath_{};
    std::string moduleId_{};
};

} // namespace HIKARI::RUNTIME
