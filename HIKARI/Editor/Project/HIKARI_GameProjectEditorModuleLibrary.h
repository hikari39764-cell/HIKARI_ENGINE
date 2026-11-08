#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "HIKARI/Project/GameProjectEditorModuleApi.h"

namespace HIKARI::EDITOR_PROJECT {

class IEditorDocumentService;
class IEditorSceneServices;
class IEditorWorkspace;

} // namespace HIKARI::EDITOR_PROJECT

namespace HIKARI::EDITOR {

class EditorOperationRegistry;
class GameProjectEditorWorkspaceRegistry;

class GameProjectEditorModuleLibrary {
public:
    GameProjectEditorModuleLibrary() = default;
    ~GameProjectEditorModuleLibrary();

    GameProjectEditorModuleLibrary(const GameProjectEditorModuleLibrary&) = delete;
    GameProjectEditorModuleLibrary& operator=(const GameProjectEditorModuleLibrary&) = delete;

    // The Editor companion is optional. Absence of the explicit launch
    // argument succeeds without loading or probing a default DLL.
    bool LoadForLaunch(std::string& outMessage);
    // 項目に入れた追加モジュールの編集器 DLL を読む。
    bool Load(const std::filesystem::path& modulePath, std::string& outMessage);
    void Reset() noexcept;

    bool IsLoaded() const noexcept;
    std::string_view GetModuleId() const noexcept;
    size_t GetWorkspaceCount() const noexcept;
    EDITOR_PROJECT::IEditorWorkspace* GetWorkspace(size_t index) const noexcept;
    size_t GetViewportToolCount() const noexcept;
    EDITOR_PROJECT::IEditorViewportTool* GetViewportTool(size_t index) const noexcept;
    size_t GetPanelCount() const noexcept;
    EDITOR_PROJECT::IEditorPanel* GetPanel(size_t index) const noexcept;
    bool RegisterEditorOperations(EditorOperationRegistry& registry, uint64_t ownerToken,
                                  EDITOR_PROJECT::IEditorDocumentService* documents,
                                  EDITOR_PROJECT::IEditorSceneServices* sceneServices, std::string& outMessage,
                                  GameProjectEditorWorkspaceRegistry* commands = nullptr) const;
    const std::filesystem::path& GetModulePath() const noexcept;

private:
    void* libraryHandle_ = nullptr;
    const EDITOR_PROJECT::GameProjectEditorModuleApi* api_ = nullptr;
    void* instance_ = nullptr;
    std::filesystem::path modulePath_{};
    std::string moduleId_{};
};

} // namespace HIKARI::EDITOR
