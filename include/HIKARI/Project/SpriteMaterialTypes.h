#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI {

inline constexpr uint32_t kGameProjectSpriteMaterialStateVersion = 1u;
inline constexpr uint32_t kGameProjectSpriteMaterialStateV1Size = 152u;
inline constexpr uint32_t kGameProjectSpriteMaterialParameterInfoVersion = 1u;
inline constexpr uint32_t kGameProjectSpriteMaterialParameterInfoV1Size = 448u;
inline constexpr size_t kGameProjectSpriteMaterialTextCapacity = 128u;

struct GameProjectSpriteMaterialState {
    uint32_t structSize = kGameProjectSpriteMaterialStateV1Size;
    uint32_t version = kGameProjectSpriteMaterialStateVersion;
    char materialAssetGuid[kGameProjectSpriteMaterialTextCapacity]{};
    uint8_t enabled = 0u;
    uint8_t reserved[3]{};
    uint32_t parameterCount = 0u;
    uint64_t stateRevision = 0u;
};

struct GameProjectSpriteMaterialParameterInfo {
    uint32_t structSize = kGameProjectSpriteMaterialParameterInfoV1Size;
    uint32_t version = kGameProjectSpriteMaterialParameterInfoVersion;
    char key[kGameProjectSpriteMaterialTextCapacity]{};
    char label[kGameProjectSpriteMaterialTextCapacity]{};
    char group[kGameProjectSpriteMaterialTextCapacity]{};
    uint32_t slot = 0u;
    GameProjectBindingValueType type = GameProjectBindingValueType::Float;
    uint8_t hasRange = 0u;
    uint8_t reserved[2]{};
    float minimum = 0.0f;
    float maximum = 0.0f;
    float step = 0.0f;
    uint32_t reserved2 = 0u;
    GameProjectBindingValue defaultValue{};
};

static_assert(std::is_standard_layout_v<GameProjectSpriteMaterialState>);
static_assert(std::is_trivially_copyable_v<GameProjectSpriteMaterialState>);
static_assert(sizeof(GameProjectSpriteMaterialState) == kGameProjectSpriteMaterialStateV1Size);
static_assert(offsetof(GameProjectSpriteMaterialState, materialAssetGuid) == 8u);
static_assert(offsetof(GameProjectSpriteMaterialState, enabled) == 136u);
static_assert(offsetof(GameProjectSpriteMaterialState, parameterCount) == 140u);
static_assert(offsetof(GameProjectSpriteMaterialState, stateRevision) == 144u);

static_assert(std::is_standard_layout_v<GameProjectSpriteMaterialParameterInfo>);
static_assert(std::is_trivially_copyable_v<GameProjectSpriteMaterialParameterInfo>);
static_assert(sizeof(GameProjectSpriteMaterialParameterInfo) == kGameProjectSpriteMaterialParameterInfoV1Size);
static_assert(offsetof(GameProjectSpriteMaterialParameterInfo, key) == 8u);
static_assert(offsetof(GameProjectSpriteMaterialParameterInfo, label) == 136u);
static_assert(offsetof(GameProjectSpriteMaterialParameterInfo, group) == 264u);
static_assert(offsetof(GameProjectSpriteMaterialParameterInfo, slot) == 392u);
static_assert(offsetof(GameProjectSpriteMaterialParameterInfo, defaultValue) == 416u);

} // namespace HIKARI
