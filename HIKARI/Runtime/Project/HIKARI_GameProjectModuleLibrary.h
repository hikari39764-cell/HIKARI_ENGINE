#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "Runtime/Project/HIKARI_GameProjectModuleApi.h"

namespace HIKARI {

    class IRuntimeExtension;

} // namespace HIKARI

namespace HIKARI::RUNTIME {

    class GameProjectModuleLibrary {
    public:
        GameProjectModuleLibrary() = default;
        ~GameProjectModuleLibrary();

        GameProjectModuleLibrary(
            const GameProjectModuleLibrary&) = delete;
        GameProjectModuleLibrary& operator=(
            const GameProjectModuleLibrary&) = delete;

        bool LoadForLaunch(std::string& outMessage);
        bool Load(
            const std::filesystem::path& modulePath,
            std::string& outMessage);
        void Reset() noexcept;

        bool IsLoaded() const noexcept;
        std::string_view GetModuleId() const noexcept;
        size_t GetRuntimeExtensionCount() const noexcept;
        IRuntimeExtension* GetRuntimeExtension(
            size_t index) const noexcept;
        const std::filesystem::path& GetModulePath() const noexcept;

    private:
        void* libraryHandle_ = nullptr;
        const GameProjectModuleApi* api_ = nullptr;
        void* instance_ = nullptr;
        std::filesystem::path modulePath_{};
        std::string moduleId_{};
    };

} // namespace HIKARI::RUNTIME
