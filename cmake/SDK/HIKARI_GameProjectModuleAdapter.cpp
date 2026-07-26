#include "HIKARI/Project/GameProjectModuleApi.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "HIKARI/Project/GameProjectModule.h"

namespace HIKARI::RUNTIME {
namespace {

    struct GameProjectModuleInstance {
        std::unique_ptr<IGameProjectModule> module{};
        std::string moduleId{};
        std::vector<std::unique_ptr<IRuntimeExtension>>
            runtimeExtensions{};
    };

    void* CreateInstance() noexcept {
        try {
            auto instance =
                std::make_unique<GameProjectModuleInstance>();
            instance->module = CreateGameProjectModule();
            if (!instance->module) {
                return nullptr;
            }
            instance->moduleId =
                instance->module->GetModuleId();
            if (instance->moduleId.empty()) {
                return nullptr;
            }
            instance->runtimeExtensions =
                instance->module->CreateRuntimeExtensions();
            for (const auto& extension :
                    instance->runtimeExtensions) {
                if (!extension ||
                    extension->GetExtensionId().empty()) {
                    return nullptr;
                }
            }
            return instance.release();
        }
        catch (...) {
            return nullptr;
        }
    }

    void DestroyInstance(void* instance) noexcept {
        delete static_cast<GameProjectModuleInstance*>(
            instance);
    }

    const char* GetModuleId(void* instance) noexcept {
        const auto* module =
            static_cast<GameProjectModuleInstance*>(
                instance);
        return module != nullptr
            ? module->moduleId.c_str()
            : nullptr;
    }

    size_t GetRuntimeExtensionCount(
        void* instance) noexcept {

        const auto* module =
            static_cast<GameProjectModuleInstance*>(
                instance);
        return module != nullptr
            ? module->runtimeExtensions.size()
            : 0u;
    }

    IRuntimeExtension* GetRuntimeExtension(
        void* instance,
        size_t index) noexcept {

        auto* module =
            static_cast<GameProjectModuleInstance*>(
                instance);
        if (module == nullptr ||
            index >= module->runtimeExtensions.size()) {
            return nullptr;
        }
        return module->runtimeExtensions[index].get();
    }

    const GameProjectModuleApi kModuleApi{
        sizeof(GameProjectModuleApi),
        kGameProjectModuleAbiVersion,
        &CreateInstance,
        &DestroyInstance,
        &GetModuleId,
        &GetRuntimeExtensionCount,
        &GetRuntimeExtension,
    };

} // namespace
} // namespace HIKARI::RUNTIME

extern "C" __declspec(dllexport)
const HIKARI::RUNTIME::GameProjectModuleApi*
    HIKARI_GetGameProjectModuleApi(
        uint32_t hostAbiVersion) noexcept {

    return hostAbiVersion ==
        HIKARI::RUNTIME::kGameProjectModuleAbiVersion
        ? &HIKARI::RUNTIME::kModuleApi
        : nullptr;
}
