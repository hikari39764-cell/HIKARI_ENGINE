#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <string>

#include "Core/Serialization/Json/HIKARI_JsonFields.h"

// 地形データ核の格子：世界分区の格子 1 つが地形タイル 1 枚。標本はセルの角に置き、タイルは (tileCells + 1)^2 個の
// 標本を持つ（隣のタイルと縁の標本を共有し、両方に同じ値を保存する）。全体の標本は 64 bit の通し番号で指す。
namespace HIKARI::TERRAIN::DATA {

struct TileCoord {
    int32_t x = 0;
    int32_t z = 0;
    auto operator<=>(const TileCoord&) const = default;
};

// 通し番号の標本の矩形（x0, z0 を含み、width × height 個）。
struct SampleRect {
    int64_t x0 = 0;
    int64_t z0 = 0;
    uint32_t width = 0u;
    uint32_t height = 0u;
    bool Empty() const noexcept { return width == 0u || height == 0u; }
    int64_t X1() const noexcept { return x0 + static_cast<int64_t>(width) - 1; }
    int64_t Z1() const noexcept { return z0 + static_cast<int64_t>(height) - 1; }
    std::size_t Count() const noexcept { return static_cast<std::size_t>(width) * height; }
    bool operator==(const SampleRect&) const = default;
};

// 地形全体の格子。タイル寸法、タイル一辺のセル数、焼いた高さ（16 bit）の量子化範囲を 1 か所で持つ。
struct TerrainGrid {
    float tileSizeMeters = 256.0f;
    uint32_t tileCells = 256u;
    float heightMin = -512.0f;
    float heightRange = 2048.0f;
    bool operator==(const TerrainGrid&) const = default;
};

inline constexpr uint32_t kTerrainTileCellChoices[] = {64u, 128u, 256u, 512u};
// 一つの操作が一度に扱う標本数の上限（8 km を 1 m 間隔で一辺 8193 標本 = 約 6700 万）。
inline constexpr std::size_t kTerrainMaximumRectSamples = std::size_t{1} << 27;

inline constexpr auto kTerrainGridFields = [](auto&& field) {
    using S = TerrainGrid;
    using namespace JSONFIELDS;
    field("tileSizeMeters", &S::tileSizeMeters, Range{16.0, 4096.0}, Label{"Tile Size"}, Unit{"m"},
          Help{"World size of one terrain tile; equals the world partition cell size."});
    field("tileCells", &S::tileCells, Range{64.0, 512.0}, Label{"Tile Cells"},
          Help{"Cells per tile edge: 64, 128, 256 or 512. Samples per edge are one more."});
    field("heightMin", &S::heightMin, Range{-100000.0, 100000.0}, Label{"Height Min"}, Unit{"m"},
          Help{"Lowest height the baked 16-bit tiles can store."});
    field("heightRange", &S::heightRange, Range{1.0, 100000.0}, Label{"Height Range"}, Unit{"m"},
          Help{"Height span of the baked 16-bit tiles; precision is range / 65535."});
};

// 格子として使えるか（有限・正・tileCells が選択肢のどれか）。失敗時は理由を返す。
bool ValidateTerrainGrid(const TerrainGrid& grid, std::string* error = nullptr);

inline uint32_t TileSamples(const TerrainGrid& grid) noexcept { return grid.tileCells + 1u; }
inline std::size_t TileSampleCount(const TerrainGrid& grid) noexcept {
    return static_cast<std::size_t>(TileSamples(grid)) * TileSamples(grid);
}
inline double SampleSpacing(const TerrainGrid& grid) noexcept {
    return static_cast<double>(grid.tileSizeMeters) / static_cast<double>(grid.tileCells);
}

// 負の数でも床に落ちる整数除算（b > 0）。
inline int64_t FloorDiv(int64_t a, int64_t b) noexcept {
    const int64_t q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}

// 世界 XZ（メートル）を含むタイル。境界上はより大きい側のタイル。
TileCoord TileOfWorld(const TerrainGrid& grid, double worldX, double worldZ) noexcept;
// タイル (tile) の局所標本 0 の通し番号。
inline int64_t TileFirstSample(const TerrainGrid& grid, int32_t tile) noexcept {
    return static_cast<int64_t>(tile) * grid.tileCells;
}
// タイルの全標本（縁を含む）の矩形。
SampleRect TileSampleRect(const TerrainGrid& grid, TileCoord tile) noexcept;
// 世界 XZ の円（半径）を覆う標本の矩形。
SampleRect SampleRectAround(const TerrainGrid& grid, double worldX, double worldZ, double radius) noexcept;
// 標本矩形のどれかの標本を保存しているタイル（縁を共有する隣も含む）を x、z の順で返す。
template <class Fn>
void ForEachTileHolding(const TerrainGrid& grid, const SampleRect& rect, Fn&& fn) {
    if (rect.Empty()) {
        return;
    }
    const int64_t cells = grid.tileCells;
    // 標本 s を持つタイル t は t * cells <= s <= t * cells + cells、つまり FloorDiv(s - 1) から FloorDiv(s) まで。
    const int64_t tx0 = FloorDiv(rect.x0 - 1, cells);
    const int64_t tz0 = FloorDiv(rect.z0 - 1, cells);
    const int64_t tx1 = FloorDiv(rect.X1(), cells);
    const int64_t tz1 = FloorDiv(rect.Z1(), cells);
    for (int64_t tz = tz0; tz <= tz1; ++tz) {
        for (int64_t tx = tx0; tx <= tx1; ++tx) {
            fn(TileCoord{static_cast<int32_t>(tx), static_cast<int32_t>(tz)});
        }
    }
}
// 二つの矩形の共通部分。
SampleRect IntersectRects(const SampleRect& a, const SampleRect& b) noexcept;

uint16_t QuantizeHeight(const TerrainGrid& grid, float height) noexcept;
float DequantizeHeight(const TerrainGrid& grid, uint16_t value) noexcept;
inline float HeightQuantizationStep(const TerrainGrid& grid) noexcept { return grid.heightRange / 65535.0f; }

} // namespace HIKARI::TERRAIN::DATA
