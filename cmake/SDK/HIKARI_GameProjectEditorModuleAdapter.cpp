#include "HIKARI/Project/GameProjectEditorModuleApi.h"

#if defined(HIKARI_SHIPPING)
#error "The GameProject Editor companion must not be built for Shipping."
#endif

#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "HIKARI/Project/GameProjectEditorModule.h"

namespace HIKARI::EDITOR_PROJECT {
namespace {

    constexpr size_t kMaximumWorkspaceCount = 64u;

    bool IsValidIdentifier(const char* value) noexcept {
        if (value == nullptr || value[0] == '\0') {
            return false;
        }
        const unsigned char first =
            static_cast<unsigned char>(value[0]);
        if (!((first >= 'a' && first <= 'z') ||
              (first >= 'A' && first <= 'Z') ||
              (first >= '0' && first <= '9'))) {
            return false;
        }
        size_t length = 0u;
        for (const unsigned char* cursor =
                reinterpret_cast<const unsigned char*>(value);
            *cursor != 0u;
            ++cursor) {
            const bool valid =
                (*cursor >= 'a' && *cursor <= 'z') ||
                (*cursor >= 'A' && *cursor <= 'Z') ||
                (*cursor >= '0' && *cursor <= '9') ||
                *cursor == '.' || *cursor == '_' || *cursor == '-';
            if (!valid || ++length > 127u) {
                return false;
            }
        }
        return true;
    }

    struct GameProjectEditorModuleInstance {
        std::unique_ptr<IGameProjectEditorModule> module{};
        std::string moduleId{};
        std::vector<std::unique_ptr<IEditorWorkspace>> workspaces{};
    };

    void* CreateInstance() noexcept {
        try {
            auto instance =
                std::make_unique<GameProjectEditorModuleInstance>();
            instance->module = CreateGameProjectEditorModule();
            if (!instance->module) {
                return nullptr;
            }
            instance->moduleId = instance->module->GetModuleId();
            if (!IsValidIdentifier(instance->moduleId.c_str())) {
                return nullptr;
            }

            instance->workspaces =
                instance->module->CreateEditorWorkspaces();
            if (instance->workspaces.size() > kMaximumWorkspaceCount) {
                return nullptr;
            }
            std::unordered_set<std::string> workspaceIds{};
            for (const auto& workspace : instance->workspaces) {
                if (!workspace) {
                    return nullptr;
                }
                const EditorWorkspaceDescriptor descriptor =
                    workspace->GetDescriptor();
                if (descriptor.structSize <
                        sizeof(EditorWorkspaceDescriptor) ||
                    descriptor.apiVersion != kEditorWorkspaceApiVersion ||
                    !IsValidIdentifier(descriptor.workspaceId) ||
                    descriptor.displayName == nullptr ||
                    descriptor.displayName[0] == '\0' ||
                    !workspaceIds.emplace(
                        descriptor.workspaceId).second) {
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
        delete static_cast<GameProjectEditorModuleInstance*>(instance);
    }

    const char* GetModuleId(void* instance) noexcept {
        const auto* module =
            static_cast<GameProjectEditorModuleInstance*>(instance);
        return module != nullptr
            ? module->moduleId.c_str()
            : nullptr;
    }

    size_t GetWorkspaceCount(void* instance) noexcept {
        const auto* module =
            static_cast<GameProjectEditorModuleInstance*>(instance);
        return module != nullptr
            ? module->workspaces.size()
            : 0u;
    }

    IEditorWorkspace* GetWorkspace(
        void* instance,
        size_t index) noexcept {

        auto* module =
            static_cast<GameProjectEditorModuleInstance*>(instance);
        if (module == nullptr || index >= module->workspaces.size()) {
            return nullptr;
        }
        return module->workspaces[index].get();
    }

    const GameProjectEditorModuleApi kModuleApi{
        sizeof(GameProjectEditorModuleApi),
        kGameProjectEditorModuleAbiVersion,
        &CreateInstance,
        &DestroyInstance,
        &GetModuleId,
        &GetWorkspaceCount,
        &GetWorkspace,
    };

} // namespace
} // namespace HIKARI::EDITOR_PROJECT

extern "C" __declspec(dllexport)
const HIKARI::EDITOR_PROJECT::GameProjectEditorModuleApi*
    HIKARI_GetGameProjectEditorModuleApi(
        uint32_t hostAbiVersion) noexcept {

    return hostAbiVersion ==
        HIKARI::EDITOR_PROJECT::kGameProjectEditorModuleAbiVersion
        ? &HIKARI::EDITOR_PROJECT::kModuleApi
        : nullptr;
}
