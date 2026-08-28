#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace HIKARI {

    class IRuntimeExtension;

} // namespace HIKARI

namespace HIKARI::RUNTIME {

    // ABI 6 replaces string-scanned component queries with stable type ids and
    // extends GameComponentDescriptor. Reject ABI 5 modules rather than
    // interpreting the changed source-level SDK layout.
    inline constexpr uint32_t kGameProjectModuleAbiVersion = 6u;
    inline constexpr char kGameProjectModuleApiEntryPoint[] =
        "HIKARI_GetGameProjectModuleApi";

    struct GameProjectModuleApi {
        uint32_t structSize = sizeof(GameProjectModuleApi);
        uint32_t abiVersion = kGameProjectModuleAbiVersion;

        void* (*createInstance)() noexcept = nullptr;
        void (*destroyInstance)(void* instance) noexcept = nullptr;
        const char* (*getModuleId)(void* instance) noexcept = nullptr;
        size_t (*getRuntimeExtensionCount)(
            void* instance) noexcept = nullptr;
        IRuntimeExtension* (*getRuntimeExtension)(
            void* instance,
            size_t index) noexcept = nullptr;
    };

    using QueryGameProjectModuleApi =
        const GameProjectModuleApi* (*)(
            uint32_t hostAbiVersion) noexcept;

    static_assert(std::is_standard_layout_v<GameProjectModuleApi>);
    static_assert(std::is_trivially_copyable_v<GameProjectModuleApi>);
    static_assert(sizeof(GameProjectModuleApi) == 48u);
    static_assert(offsetof(GameProjectModuleApi, structSize) == 0u);
    static_assert(offsetof(GameProjectModuleApi, abiVersion) == 4u);
    static_assert(offsetof(GameProjectModuleApi, createInstance) == 8u);
    static_assert(offsetof(GameProjectModuleApi, destroyInstance) == 16u);
    static_assert(offsetof(GameProjectModuleApi, getModuleId) == 24u);
    static_assert(
        offsetof(GameProjectModuleApi, getRuntimeExtensionCount) == 32u);
    static_assert(
        offsetof(GameProjectModuleApi, getRuntimeExtension) == 40u);

} // namespace HIKARI::RUNTIME
