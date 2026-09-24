#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include "HIKARI/Project/GameProjectEditorModuleApi.h"

namespace HIKARI::EDITOR_PROJECT {

class IEditorWorkspace;

} // namespace HIKARI::EDITOR_PROJECT

namespace HIKARI::EDITOR {

class GameProjectEditorModuleLibrary {
public:
    GameProjectEditorModuleLibrary() = default;
    ~GameProjectEditorModuleLibrary();

    GameProjectEditorModuleLibrary(const GameProjectEditorModuleLibrary&) = delete;
    GameProjectEditorModuleLibrary& operator=(const GameProjectEditorModuleLibrary&) = delete;

    // The Editor companion is optional. Absence of the explicit launch
    // argument succeeds without loading or probing a default DLL.
    bool LoadForLaunch(std::string& outMessage);
    void Reset() noexcept;

    bool IsLoaded() const noexcept;
    std::string_view GetModuleId() const noexcept;
    size_t GetWorkspaceCount() const noexcept;
    EDITOR_PROJECT::IEditorWorkspace* GetWorkspace(size_t index) const noexcept;
    const std::filesystem::path& GetModulePath() const noexcept;

private:
    bool Load(const std::filesystem::path& modulePath, std::string& outMessage);

    void* libraryHandle_ = nullptr;
    const EDITOR_PROJECT::GameProjectEditorModuleApi* api_ = nullptr;
    void* instance_ = nullptr;
    std::filesystem::path modulePath_{};
    std::string moduleId_{};
};

} // namespace HIKARI::EDITOR
