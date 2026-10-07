#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "HIKARI/Project/GameProjectSession.h"

namespace HIKARI {

class RuntimeFeatureCatalog;

} // namespace HIKARI

namespace HIKARI::RUNTIME {

// ABI 8: モジュールは Feature をカタログへ直接登録する（拡張オブジェクトの受け渡しを廃止）。
inline constexpr uint32_t kGameProjectModuleAbiVersion = 8u;
inline constexpr char kGameProjectModuleApiEntryPoint[] = "HIKARI_GetGameProjectModuleApi";

struct GameProjectModuleApi {
    uint32_t structSize = sizeof(GameProjectModuleApi);
    uint32_t abiVersion = kGameProjectModuleAbiVersion;

    void* (*createInstance)() noexcept = nullptr;
    void (*destroyInstance)(void* instance) noexcept = nullptr;
    const char* (*getModuleId)(void* instance) noexcept = nullptr;
    bool (*registerRuntimeFeatures)(void* instance, RuntimeFeatureCatalog* catalog) noexcept = nullptr;
    bool (*createGameSession)(void* instance, IGameProjectSession** outSession) noexcept = nullptr;
    void (*destroyGameSession)(void* instance) noexcept = nullptr;
};

using QueryGameProjectModuleApi = const GameProjectModuleApi* (*)(uint32_t hostAbiVersion) noexcept;

static_assert(std::is_standard_layout_v<GameProjectModuleApi>);
static_assert(std::is_trivially_copyable_v<GameProjectModuleApi>);
static_assert(sizeof(GameProjectModuleApi) == 56u);
static_assert(offsetof(GameProjectModuleApi, structSize) == 0u);
static_assert(offsetof(GameProjectModuleApi, abiVersion) == 4u);
static_assert(offsetof(GameProjectModuleApi, createInstance) == 8u);
static_assert(offsetof(GameProjectModuleApi, destroyInstance) == 16u);
static_assert(offsetof(GameProjectModuleApi, getModuleId) == 24u);
static_assert(offsetof(GameProjectModuleApi, registerRuntimeFeatures) == 32u);
static_assert(offsetof(GameProjectModuleApi, createGameSession) == 40u);
static_assert(offsetof(GameProjectModuleApi, destroyGameSession) == 48u);

} // namespace HIKARI::RUNTIME
