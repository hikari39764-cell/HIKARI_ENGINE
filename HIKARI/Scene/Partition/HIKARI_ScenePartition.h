#pragma once

#include <array>
#include <cmath>
#include <compare>
#include <cstdint>
#include <cstdio>
#include <string>

#include "Core/Serialization/Json/HIKARI_JsonFieldOptions.h"

namespace HIKARI {

// 分区場景の格子。XZ 平面の正方形で、原点は世界の中心。一格に地形瓦片一枚・実例集・普通物体が載る。
inline constexpr std::array<uint32_t, 3> kWorldCellSizesMeters{128u, 256u, 512u};
inline constexpr uint32_t kDefaultWorldCellSizeMeters = 256u;

struct WorldCellCoord {
    int32_t x = 0;
    int32_t z = 0;

    auto operator<=>(const WorldCellCoord&) const = default;
};

// 開いた場景は根の物体を格子ごとの別ファイルへ保存する。主ファイルには場景の設定・系統・常駐物体が残る。
struct ScenePartitionSettings {
    bool enabled = false;
    uint32_t cellSizeMeters = kDefaultWorldCellSizeMeters;

    bool operator==(const ScenePartitionSettings&) const = default;
};

inline constexpr auto kScenePartitionFields = [](auto&& field) {
    using S = ScenePartitionSettings;
    using namespace JSONFIELDS;
    field("enabled", &S::enabled, Label{"Partitioned"});
    field("cellSize", &S::cellSizeMeters, Range{128.0, 512.0}, Unit{"m"}, Label{"Cell size"});
};

inline bool IsWorldCellSize(uint32_t meters) noexcept {
    for (uint32_t size : kWorldCellSizesMeters) {
        if (size == meters) {
            return true;
        }
    }
    return false;
}

// 選べる大きさのうち一番近いもの（同じ距離なら小さい方）。
inline uint32_t SnapWorldCellSize(uint32_t meters) noexcept {
    uint32_t best = kWorldCellSizesMeters[0];
    for (uint32_t size : kWorldCellSizesMeters) {
        const uint32_t distance = size > meters ? size - meters : meters - size;
        const uint32_t bestDistance = best > meters ? best - meters : meters - best;
        if (distance < bestDistance) {
            best = size;
        }
    }
    return best;
}

inline WorldCellCoord WorldCellAt(float x, float z, uint32_t cellSizeMeters) noexcept {
    const float size = static_cast<float>(cellSizeMeters);
    return {static_cast<int32_t>(std::floor(x / size)), static_cast<int32_t>(std::floor(z / size))};
}

// 格子ファイル名の幹（"x+003_z-012"）。
inline std::string WorldCellFileStem(WorldCellCoord cell) {
    char text[32]{};
    std::snprintf(text, sizeof(text), "x%+04d_z%+04d", static_cast<int>(cell.x), static_cast<int>(cell.z));
    return text;
}

} // namespace HIKARI
