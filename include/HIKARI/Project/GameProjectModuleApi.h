#pragma once

#include <cstddef>
#include <cstdint>

namespace HIKARI {

    class IRuntimeExtension;

} // namespace HIKARI

namespace HIKARI::RUNTIME {

    // ABI 3 adds GameComponentContext-aware lifecycle virtuals. Reject older
    // project DLLs so the host rebuilds them instead of calling a mismatched
    // IGameComponent vtable.
    inline constexpr uint32_t kGameProjectModuleAbiVersion = 3u;
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

} // namespace HIKARI::RUNTIME
