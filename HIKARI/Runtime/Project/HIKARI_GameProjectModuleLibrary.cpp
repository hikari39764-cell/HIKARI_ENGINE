#include "Runtime/Project/HIKARI_GameProjectModuleLibrary.h"

#include <Windows.h>

#include <array>
#include <system_error>
#include <utility>

#include "Platform/Paths/HIKARI_ExecutablePath.h"
#include "Runtime/HIKARI_RuntimeLaunchConfig.h"

namespace HIKARI::RUNTIME {
namespace {

std::string MakeWindowsError(const char* operation, DWORD error) {
    std::array<char, 1024> buffer{};
    const DWORD length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0u,
                                        buffer.data(), static_cast<DWORD>(buffer.size()), nullptr);
    std::string message = operation;
    message += " failed";
    if (length != 0u) {
        message += ": ";
        message.append(buffer.data(), length);
    } else {
        message += " with Win32 error " + std::to_string(error);
    }
    return message;
}

} // namespace

GameProjectModuleLibrary::~GameProjectModuleLibrary() { Reset(); }

bool GameProjectModuleLibrary::LoadForLaunch(bool allowMissing, std::string& outMessage) {
    if (const auto explicitPath = ReadRuntimeCommandLinePath(L"--project-module")) {
        return Load(*explicitPath, outMessage);
    }
    if (allowMissing) {
        Reset();
        outMessage = "GameProject module is unavailable; "
                     "authoring fallback is active.";
        return true;
    }
    return Load(PLATFORM::GetExecutableDirectory() / "HIKARI_GameProject.dll", outMessage);
}

bool GameProjectModuleLibrary::Load(const std::filesystem::path& modulePath, std::string& outMessage) {
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
        outMessage = "GameProject module was not found: " + resolved.generic_string();
        return false;
    }

    HMODULE library = LoadLibraryW(resolved.c_str());
    if (library == nullptr) {
        outMessage = MakeWindowsError("Loading GameProject module", GetLastError());
        return false;
    }

    const FARPROC entryPoint = GetProcAddress(library, kGameProjectModuleApiEntryPoint);
    if (entryPoint == nullptr) {
        outMessage = "GameProject module is missing " + std::string(kGameProjectModuleApiEntryPoint) + ": " +
                     resolved.generic_string();
        FreeLibrary(library);
        return false;
    }

    const auto queryApi = reinterpret_cast<QueryGameProjectModuleApi>(entryPoint);
    const GameProjectModuleApi* api = queryApi(kGameProjectModuleAbiVersion);
    if (api == nullptr || api->structSize < sizeof(GameProjectModuleApi) ||
        api->abiVersion != kGameProjectModuleAbiVersion || api->createInstance == nullptr ||
        api->destroyInstance == nullptr || api->getModuleId == nullptr || api->getRuntimeExtensionCount == nullptr ||
        api->getRuntimeExtension == nullptr || api->createGameSession == nullptr ||
        api->destroyGameSession == nullptr) {
        outMessage = "GameProject module ABI is incompatible. "
                     "Rebuild the project with this HIKARI engine.";
        FreeLibrary(library);
        return false;
    }

    void* instance = api->createInstance();
    if (instance == nullptr) {
        outMessage = "GameProject module instance creation failed: " + resolved.generic_string();
        FreeLibrary(library);
        return false;
    }
    const char* moduleId = api->getModuleId(instance);
    if (moduleId == nullptr || moduleId[0] == '\0') {
        api->destroyInstance(instance);
        FreeLibrary(library);
        outMessage = "GameProject module ID is empty: " + resolved.generic_string();
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

void GameProjectModuleLibrary::Reset() noexcept {
    DestroyGameSession();
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

bool GameProjectModuleLibrary::IsLoaded() const noexcept {
    return libraryHandle_ != nullptr && api_ != nullptr && instance_ != nullptr;
}

std::string_view GameProjectModuleLibrary::GetModuleId() const noexcept { return moduleId_; }

size_t GameProjectModuleLibrary::GetRuntimeExtensionCount() const noexcept {
    return IsLoaded() ? api_->getRuntimeExtensionCount(instance_) : 0u;
}

IRuntimeExtension* GameProjectModuleLibrary::GetRuntimeExtension(size_t index) const noexcept {
    return IsLoaded() ? api_->getRuntimeExtension(instance_, index) : nullptr;
}

bool GameProjectModuleLibrary::CreateGameSession(std::string& outMessage) {
    DestroyGameSession();
    if (!IsLoaded()) {
        outMessage.clear();
        return true;
    }

    HIKARI::IGameProjectSession* session = nullptr;
    if (!api_->createGameSession(instance_, &session)) {
        outMessage = "GameProject module failed to create its game session.";
        return false;
    }

    gameSession_ = session;
    outMessage.clear();
    return true;
}

HIKARI::IGameProjectSession* GameProjectModuleLibrary::GetGameSession() const noexcept { return gameSession_; }

void GameProjectModuleLibrary::DestroyGameSession() noexcept {
    if (gameSession_ != nullptr && api_ != nullptr && instance_ != nullptr && api_->destroyGameSession != nullptr) {
        api_->destroyGameSession(instance_);
    }
    gameSession_ = nullptr;
}

const std::filesystem::path& GameProjectModuleLibrary::GetModulePath() const noexcept { return modulePath_; }

} // namespace HIKARI::RUNTIME
