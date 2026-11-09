#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "HIKARI/Project/WorldMassTypes.h"

// 実例集：同じ模型の大量の個体（木、岩、草むらなど）。物体を作らず、格子ごとの配列として世界が持つ。
// 個体は 32 B で、物体の枠・名前・組件を持たない。編集器の配置と散布の結果はここへ書く。
namespace HIKARI {

inline constexpr uint32_t kGameProjectWorldInstanceVersion = 1u;
inline constexpr uint32_t kGameProjectWorldInstanceSetDescV1Size = 232u;
inline constexpr uint32_t kGameProjectWorldInstanceNameCapacity = 96u;

enum class GameProjectWorldInstanceFlags : uint16_t {
    None = 0u,
    // 手で置いた個体。規則で作り直しても残す。
    HandPlaced = 1u << 0u,
};

enum class GameProjectWorldInstanceSetFlags : uint32_t {
    None = 0u,
    ReceiveShadow = 1u << 0u,
    // 光線追跡の場景へ入れる（影と GI に効く）。草のように数が多く細かいものは入れない。
    RayTraced = 1u << 1u,
};

constexpr GameProjectWorldInstanceSetFlags operator|(GameProjectWorldInstanceSetFlags lhs,
                                                     GameProjectWorldInstanceSetFlags rhs) noexcept {
    return static_cast<GameProjectWorldInstanceSetFlags>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

struct GameProjectWorldInstance {
    GameProjectFloat3 position{};
    int16_t rotation[4] = {0, 0, 0, 32767};          // 四元数の snorm16
    uint16_t scale[3] = {0x3C00u, 0x3C00u, 0x3C00u}; // 半精度の拡縮
    uint16_t flags = 0u;
    uint32_t seed = 0u; // 個体ごとの色の揺らぎと風の位相
};

// 実例集の定義。name が世界の中の識別子で、MakeComponentTypeId(name) がその ID。
struct GameProjectWorldInstanceSetDesc {
    uint32_t structSize = kGameProjectWorldInstanceSetDescV1Size;
    uint32_t version = kGameProjectWorldInstanceVersion;
    char name[kGameProjectWorldInstanceNameCapacity]{};
    GameProjectWorldMassRenderGeometry geometry = GameProjectWorldMassRenderGeometry::ModelAsset;
    uint32_t flags = static_cast<uint32_t>(GameProjectWorldInstanceSetFlags::ReceiveShadow |
                                           GameProjectWorldInstanceSetFlags::RayTraced);
    char modelAssetGuid[kGameProjectWorldInstanceNameCapacity]{};
    uint32_t meshIndex = 0u;
    uint32_t primitiveIndex = kGameProjectWorldMassAllPrimitives; // 既定は網の全部の部品
    uint32_t materialIndex = 0u;
    float tintVariation = 0.0f; // 個体ごとの色の揺らぎ（0..1）
    float windStrength = 0.0f;  // 風で揺れる強さ（0..1）
    // 画面の高さに対する個体の大きさの割合がこれより小さければ描かない（0 は常に描く）。上限 0.0255。
    // 0.001 は 1440p で約 1.4 画素。小さな物から先に消えるので、距離を種類ごとに決めなくてよい。
    float minScreenHeight = 0.001f;
};

static_assert(std::is_trivially_copyable_v<GameProjectWorldInstance>);
static_assert(sizeof(GameProjectWorldInstance) == 32u);
static_assert(offsetof(GameProjectWorldInstance, rotation) == 12u);
static_assert(offsetof(GameProjectWorldInstance, scale) == 20u);
static_assert(offsetof(GameProjectWorldInstance, flags) == 26u);
static_assert(offsetof(GameProjectWorldInstance, seed) == 28u);
static_assert(std::is_trivially_copyable_v<GameProjectWorldInstanceSetDesc>);
static_assert(sizeof(GameProjectWorldInstanceSetDesc) == kGameProjectWorldInstanceSetDescV1Size);

namespace WORLDINSTANCE {

inline uint16_t EncodeHalf(float value) noexcept {
    uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint32_t sign = (bits >> 16u) & 0x8000u;
    const int32_t exponent = static_cast<int32_t>((bits >> 23u) & 0xFFu) - 112;
    const uint32_t mantissa = bits & 0x7FFFFFu;
    if (exponent <= 0) {
        return static_cast<uint16_t>(sign); // 小さすぎる値は 0
    }
    if (exponent >= 31) {
        return static_cast<uint16_t>(sign | 0x7BFFu); // 大きすぎる値は最大の有限値（NaN も含む）
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10u) | (mantissa >> 13u));
}

inline float DecodeHalf(uint16_t value) noexcept {
    const uint32_t sign = static_cast<uint32_t>(value & 0x8000u) << 16u;
    const uint32_t exponent = (value >> 10u) & 0x1Fu;
    const uint32_t mantissa = value & 0x3FFu;
    uint32_t bits = sign;
    if (exponent != 0u) {
        bits |= ((exponent + 112u) << 23u) | (mantissa << 13u);
    }
    float result = 0.0f;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

inline int16_t EncodeSnorm16(float value) noexcept {
    return static_cast<int16_t>(std::lround(std::clamp(value, -1.0f, 1.0f) * 32767.0f));
}

inline float DecodeSnorm16(int16_t value) noexcept { return static_cast<float>(value) / 32767.0f; }

inline GameProjectWorldInstance Encode(
    const GameProjectFloat3& position, const GameProjectQuaternion& rotation, const GameProjectFloat3& scale,
    uint32_t seed, GameProjectWorldInstanceFlags flags = GameProjectWorldInstanceFlags::None) noexcept {
    GameProjectWorldInstance out{};
    out.position = position;
    out.rotation[0] = EncodeSnorm16(rotation.x);
    out.rotation[1] = EncodeSnorm16(rotation.y);
    out.rotation[2] = EncodeSnorm16(rotation.z);
    out.rotation[3] = EncodeSnorm16(rotation.w);
    out.scale[0] = EncodeHalf(scale.x);
    out.scale[1] = EncodeHalf(scale.y);
    out.scale[2] = EncodeHalf(scale.z);
    out.flags = static_cast<uint16_t>(flags);
    out.seed = seed;
    return out;
}

} // namespace WORLDINSTANCE

} // namespace HIKARI
