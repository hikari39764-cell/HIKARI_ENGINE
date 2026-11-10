#include "Editor/Project/HIKARI_GameProjectEditorModuleLibrary.h"

#include <Windows.h>

#include <system_error>
#include <utility>

#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Projects/HIKARI_ProjectProcess.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Project/HIKARI_GameProjectEditorOperationRegistration.h"
#include "Runtime/HIKARI_RuntimeLaunchConfig.h"

namespace HIKARI::EDITOR {
namespace {

constexpr size_t kMaximumWorkspaceCount = 64u;

} // namespace

GameProjectEditorModuleLibrary::~GameProjectEditorModuleLibrary() { Reset(); }

bool GameProjectEditorModuleLibrary::LoadForLaunch(std::string& outMessage) {
    const auto explicitPath = ReadRuntimeCommandLinePath(L"--project-editor-module");
    if (!explicitPath) {
        Reset();
        outMessage.clear();
        return true;
    }
    return Load(*explicitPath, outMessage);
}

bool GameProjectEditorModuleLibrary::Load(const std::filesystem::path& modulePath, std::string& outMessage) {
    Reset();
    std::error_code ec{};
    std::filesystem::path resolved = std::filesystem::weakly_canonical(modulePath, ec);
    if (ec || resolved.empty()) {
        ec.clear();
        resolved = std::filesystem::absolute(modulePath, ec);
    }
    if (ec || resolved.empty()) {
        resolved = modulePath.lexically_normal();
    }
    if (!std::filesystem::is_regular_file(resolved, ec) || ec) {
        outMessage = "GameProject Editor companion was not found: " + TEXT::PathToUtf8(resolved);
        return false;
    }

    HMODULE library =
        LoadLibraryExW(resolved.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (library == nullptr) {
        outMessage = PROJECT_PROCESS::MakeWindowsError("Loading GameProject Editor companion", GetLastError());
        return false;
    }

    const FARPROC entryPoint = GetProcAddress(library, EDITOR_PROJECT::kGameProjectEditorModuleApiEntryPoint);
    if (entryPoint == nullptr) {
        outMessage = "GameProject Editor companion is missing " +
                     std::string(EDITOR_PROJECT::kGameProjectEditorModuleApiEntryPoint) + ": " +
                     TEXT::PathToUtf8(resolved);
        FreeLibrary(library);
        return false;
    }

    const auto queryApi = reinterpret_cast<EDITOR_PROJECT::QueryGameProjectEditorModuleApi>(entryPoint);
    const EDITOR_PROJECT::GameProjectEditorModuleApi* api =
        queryApi(EDITOR_PROJECT::kGameProjectEditorModuleAbiVersion);
    if (api == nullptr || api->structSize < sizeof(EDITOR_PROJECT::GameProjectEditorModuleApi) ||
        api->abiVersion != EDITOR_PROJECT::kGameProjectEditorModuleAbiVersion || api->createInstance == nullptr ||
        api->destroyInstance == nullptr || api->getModuleId == nullptr || api->getWorkspaceCount == nullptr ||
        api->getWorkspace == nullptr || api->registerEditorOperations == nullptr ||
        api->getViewportToolCount == nullptr || api->getViewportTool == nullptr || api->getPanelCount == nullptr ||
        api->getPanel == nullptr || api->collectGameExportFiles == nullptr) {
        outMessage = "GameProject Editor companion ABI is incompatible. "
                     "Rebuild the project with this HIKARI Editor SDK.";
        FreeLibrary(library);
        return false;
    }

    void* instance = api->createInstance();
    if (instance == nullptr) {
        outMessage = "GameProject Editor companion instance creation failed: " + TEXT::PathToUtf8(resolved);
        FreeLibrary(library);
        return false;
    }

    const char* moduleId = api->getModuleId(instance);
    const size_t workspaceCount = api->getWorkspaceCount(instance);
    bool valid = moduleId != nullptr && moduleId[0] != '\0' && workspaceCount <= kMaximumWorkspaceCount;
    for (size_t index = 0u; valid && index < workspaceCount; ++index) {
        valid = api->getWorkspace(instance, index) != nullptr;
    }
    if (!valid) {
        api->destroyInstance(instance);
        FreeLibrary(library);
        outMessage = "GameProject Editor companion returned invalid module "
                     "metadata or workspace ownership.";
        return false;
    }

    libraryHandle_ = library;
    api_ = api;
    instance_ = instance;
    modulePath_ = std::move(resolved);
    moduleId_ = moduleId;
    outMessage.clear();
    return true;
}

void GameProjectEditorModuleLibrary::Reset() noexcept {
    if (api_ != nullptr && instance_ != nullptr && api_->destroyInstance != nullptr) {
        api_->destroyInstance(instance_);
    }
    instance_ = nullptr;
    api_ = nullptr;
    moduleId_.clear();
    modulePath_.clear();
    if (libraryHandle_ != nullptr) {
        FreeLibrary(static_cast<HMODULE>(libraryHandle_));
        libraryHandle_ = nullptr;
    }
}

bool GameProjectEditorModuleLibrary::IsLoaded() const noexcept {
    return libraryHandle_ != nullptr && api_ != nullptr && instance_ != nullptr;
}

std::string_view GameProjectEditorModuleLibrary::GetModuleId() const noexcept { return moduleId_; }

size_t GameProjectEditorModuleLibrary::GetViewportToolCount() const noexcept {
    return IsLoaded() ? api_->getViewportToolCount(instance_) : 0u;
}

EDITOR_PROJECT::IEditorViewportTool* GameProjectEditorModuleLibrary::GetViewportTool(size_t index) const noexcept {
    return IsLoaded() ? api_->getViewportTool(instance_, index) : nullptr;
}

size_t GameProjectEditorModuleLibrary::GetPanelCount() const noexcept {
    return IsLoaded() ? api_->getPanelCount(instance_) : 0u;
}

EDITOR_PROJECT::IEditorPanel* GameProjectEditorModuleLibrary::GetPanel(size_t index) const noexcept {
    return IsLoaded() ? api_->getPanel(instance_, index) : nullptr;
}

size_t GameProjectEditorModuleLibrary::GetWorkspaceCount() const noexcept {
    return IsLoaded() ? api_->getWorkspaceCount(instance_) : 0u;
}

EDITOR_PROJECT::IEditorWorkspace* GameProjectEditorModuleLibrary::GetWorkspace(size_t index) const noexcept {
    return IsLoaded() ? api_->getWorkspace(instance_, index) : nullptr;
}

bool GameProjectEditorModuleLibrary::RegisterEditorOperations(EditorOperationRegistry& registry, uint64_t ownerToken,
                                                              EDITOR_PROJECT::IEditorDocumentService* documents,
                                                              EDITOR_PROJECT::IEditorSceneServices* sceneServices,
                                                              std::string& outMessage,
                                                              GameProjectEditorWorkspaceRegistry* commands) const {
    outMessage.clear();
    if (!IsLoaded() || ownerToken == 0u) {
        outMessage = "GameProject Editor operation registration requires a loaded module and owner token.";
        return false;
    }
    GameProjectEditorOperationRegistration registrar{registry, ownerToken, documents, commands, sceneServices};
    if (!api_->registerEditorOperations(instance_, &registrar)) {
        outMessage = "GameProject Editor operation registration callback failed.";
        return false;
    }
    if (registrar.Failed()) {
        outMessage = registrar.FailureMessage();
        return false;
    }
    return true;
}

bool GameProjectEditorModuleLibrary::CollectGameExportFiles(EDITOR_PROJECT::IGameExportFiles& files) const noexcept {
    return IsLoaded() && api_->collectGameExportFiles(instance_, &files);
}

const std::filesystem::path& GameProjectEditorModuleLibrary::GetModulePath() const noexcept { return modulePath_; }

} // namespace HIKARI::EDITOR
