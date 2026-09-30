#include "HIKARI/Project/GameProjectModuleApi.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "HIKARI/Project/GameProjectModule.h"

#if defined(HIKARI_GAME_PROJECT_HOST_IMPORT_NAME)
#include <Windows.h>
#include <delayimp.h>
#include <cstring>

namespace {
FARPROC WINAPI BindRunningGameHost(unsigned notification, PDelayLoadInfo info) {
    if (notification == dliNotePreLoadLibrary && info &&
        std::strcmp(info->szDll, HIKARI_GAME_PROJECT_HOST_IMPORT_NAME) == 0) {
        return reinterpret_cast<FARPROC>(GetModuleHandleW(nullptr));
    }
    return nullptr;
}
bool ResolveRunningGameHost() noexcept {
    __try {
        const HRESULT result = __HrLoadAllImportsForDll(HIKARI_GAME_PROJECT_HOST_IMPORT_NAME);
        return SUCCEEDED(result) || result == HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("[GameProject] Running host does not provide the required SDK exports.\n");
        return false;
    }
}
} // namespace
extern "C" const PfnDliHook __pfnDliNotifyHook2 = BindRunningGameHost;
#endif

namespace HIKARI::RUNTIME {
namespace {

struct GameProjectModuleInstance {
    std::unique_ptr<IGameProjectModule> module{};
    std::string moduleId{};
    std::vector<std::unique_ptr<IRuntimeExtension>> runtimeExtensions{};
    std::unique_ptr<IGameProjectSession> gameSession{};
};

void* CreateInstance() noexcept {
    try {
        auto instance = std::make_unique<GameProjectModuleInstance>();
        instance->module = CreateGameProjectModule();
        if (!instance->module) {
            return nullptr;
        }
        instance->moduleId = instance->module->GetModuleId();
        if (instance->moduleId.empty()) {
            return nullptr;
        }
        instance->runtimeExtensions = instance->module->CreateRuntimeExtensions();
        for (const auto& extension : instance->runtimeExtensions) {
            if (!extension || extension->GetExtensionId().empty()) {
                return nullptr;
            }
        }
        return instance.release();
    } catch (...) {
        return nullptr;
    }
}

void DestroyInstance(void* instance) noexcept { delete static_cast<GameProjectModuleInstance*>(instance); }

const char* GetModuleId(void* instance) noexcept {
    const auto* module = static_cast<GameProjectModuleInstance*>(instance);
    return module != nullptr ? module->moduleId.c_str() : nullptr;
}

size_t GetRuntimeExtensionCount(void* instance) noexcept {
    const auto* module = static_cast<GameProjectModuleInstance*>(instance);
    return module != nullptr ? module->runtimeExtensions.size() : 0u;
}

IRuntimeExtension* GetRuntimeExtension(void* instance, size_t index) noexcept {
    auto* module = static_cast<GameProjectModuleInstance*>(instance);
    if (module == nullptr || index >= module->runtimeExtensions.size()) {
        return nullptr;
    }
    return module->runtimeExtensions[index].get();
}

bool CreateGameSession(void* instance, IGameProjectSession** outSession) noexcept {
    if (outSession == nullptr) {
        return false;
    }
    *outSession = nullptr;
    auto* module = static_cast<GameProjectModuleInstance*>(instance);
    if (module == nullptr || !module->module || module->gameSession) {
        return false;
    }
    try {
        module->gameSession = module->module->CreateGameSession();
        *outSession = module->gameSession.get();
        return true;
    } catch (...) {
        module->gameSession.reset();
        return false;
    }
}

void DestroyGameSession(void* instance) noexcept {
    auto* module = static_cast<GameProjectModuleInstance*>(instance);
    if (module != nullptr) {
        module->gameSession.reset();
    }
}

const GameProjectModuleApi kModuleApi{
    sizeof(GameProjectModuleApi), kGameProjectModuleAbiVersion, &CreateInstance,    &DestroyInstance,    &GetModuleId,
    &GetRuntimeExtensionCount,    &GetRuntimeExtension,         &CreateGameSession, &DestroyGameSession,
};

} // namespace
} // namespace HIKARI::RUNTIME

extern "C" __declspec(dllexport) const HIKARI::RUNTIME::GameProjectModuleApi* HIKARI_GetGameProjectModuleApi(
    uint32_t hostAbiVersion) noexcept {
#if defined(HIKARI_GAME_PROJECT_HOST_IMPORT_NAME)
    if (!ResolveRunningGameHost())
        return nullptr;
#endif

    return hostAbiVersion == HIKARI::RUNTIME::kGameProjectModuleAbiVersion ? &HIKARI::RUNTIME::kModuleApi : nullptr;
}
