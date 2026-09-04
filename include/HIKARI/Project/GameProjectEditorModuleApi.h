#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace HIKARI::EDITOR_PROJECT {

    class IEditorWorkspace;

    inline constexpr uint32_t kGameProjectEditorModuleAbiVersion = 1u;
    inline constexpr char kGameProjectEditorModuleApiEntryPoint[] =
        "HIKARI_GetGameProjectEditorModuleApi";

    struct GameProjectEditorModuleApi {
        uint32_t structSize = sizeof(GameProjectEditorModuleApi);
        uint32_t abiVersion = kGameProjectEditorModuleAbiVersion;

        void* (*createInstance)() noexcept = nullptr;
        void (*destroyInstance)(void* instance) noexcept = nullptr;
        const char* (*getModuleId)(void* instance) noexcept = nullptr;
        size_t (*getWorkspaceCount)(void* instance) noexcept = nullptr;
        IEditorWorkspace* (*getWorkspace)(
            void* instance,
            size_t index) noexcept = nullptr;
    };

    using QueryGameProjectEditorModuleApi =
        const GameProjectEditorModuleApi* (*)(
            uint32_t hostAbiVersion) noexcept;

    static_assert(std::is_standard_layout_v<GameProjectEditorModuleApi>);
    static_assert(std::is_trivially_copyable_v<GameProjectEditorModuleApi>);
    static_assert(sizeof(GameProjectEditorModuleApi) == 48u);
    static_assert(offsetof(GameProjectEditorModuleApi, structSize) == 0u);
    static_assert(offsetof(GameProjectEditorModuleApi, abiVersion) == 4u);
    static_assert(offsetof(GameProjectEditorModuleApi, createInstance) == 8u);
    static_assert(offsetof(GameProjectEditorModuleApi, destroyInstance) == 16u);
    static_assert(offsetof(GameProjectEditorModuleApi, getModuleId) == 24u);
    static_assert(offsetof(GameProjectEditorModuleApi, getWorkspaceCount) == 32u);
    static_assert(offsetof(GameProjectEditorModuleApi, getWorkspace) == 40u);

} // namespace HIKARI::EDITOR_PROJECT
