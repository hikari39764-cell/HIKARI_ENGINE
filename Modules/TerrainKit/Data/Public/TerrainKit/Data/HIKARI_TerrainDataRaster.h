#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "TerrainKit/Data/HIKARI_TerrainDataGrid.h"

// タイル単位の標本配列と、タイルを疎に持つ全体配列。書き込みは縁を共有する全タイルへ同じ値を入れるので、
// 隣り合うタイルの縁の標本は常に一致する。無いタイルの標本は既定値として読む。
namespace HIKARI::TERRAIN::DATA {

template <class T>
struct TileRaster {
    uint32_t samples = 0u;
    std::vector<T> values{};

    TileRaster() = default;
    TileRaster(uint32_t sampleCount, T fill)
        : samples(sampleCount), values(static_cast<std::size_t>(sampleCount) * sampleCount, fill) {}

    bool Valid() const noexcept {
        return samples >= 2u && values.size() == static_cast<std::size_t>(samples) * samples;
    }
    // 範囲外は nullptr（呼び出し側で境界を確かめない入口用）。
    const T* TryAt(uint32_t x, uint32_t z) const noexcept {
        return x < samples && z < samples ? &values[static_cast<std::size_t>(z) * samples + x] : nullptr;
    }
    T* TryAt(uint32_t x, uint32_t z) noexcept {
        return x < samples && z < samples ? &values[static_cast<std::size_t>(z) * samples + x] : nullptr;
    }
    bool operator==(const TileRaster&) const = default;
};

template <class T>
class TiledRaster {
public:
    TiledRaster() = default;
    TiledRaster(const TerrainGrid& grid, T defaultValue)
        : cells_(grid.tileCells), samples_(TileSamples(grid)), defaultValue_(defaultValue) {}

    uint32_t Samples() const noexcept { return samples_; }
    T DefaultValue() const noexcept { return defaultValue_; }
    std::size_t TileCount() const noexcept { return tiles_.size(); }
    const std::map<TileCoord, TileRaster<T>>& Tiles() const noexcept { return tiles_; }

    const TileRaster<T>* Find(TileCoord tile) const noexcept {
        const auto it = tiles_.find(tile);
        return it != tiles_.end() ? &it->second : nullptr;
    }
    TileRaster<T>* Find(TileCoord tile) noexcept {
        const auto it = tiles_.find(tile);
        return it != tiles_.end() ? &it->second : nullptr;
    }
    TileRaster<T>& Ensure(TileCoord tile) {
        auto [it, inserted] = tiles_.try_emplace(tile);
        if (inserted) {
            it->second = TileRaster<T>(samples_, defaultValue_);
        }
        return it->second;
    }
    // 寸法の合うタイルだけ受け取る。
    bool Assign(TileCoord tile, TileRaster<T> raster) {
        if (raster.samples != samples_ || !raster.Valid()) {
            return false;
        }
        tiles_[tile] = std::move(raster);
        return true;
    }
    bool Erase(TileCoord tile) { return tiles_.erase(tile) != 0u; }
    void Clear() noexcept { tiles_.clear(); }

    // 通し番号の標本 1 つ。持っているタイルのどれから読んでも同じ値。
    T Get(int64_t sampleX, int64_t sampleZ) const noexcept {
        const int64_t cells = cells_;
        const int64_t tx = FloorDiv(sampleX, cells);
        const int64_t tz = FloorDiv(sampleZ, cells);
        const TileRaster<T>* raster = Find(TileCoord{static_cast<int32_t>(tx), static_cast<int32_t>(tz)});
        if (raster == nullptr) {
            return defaultValue_;
        }
        return raster->values[static_cast<std::size_t>(sampleZ - tz * cells) * samples_ +
                              static_cast<std::size_t>(sampleX - tx * cells)];
    }

    // 矩形を読む（行優先、x が内側）。無いタイルの部分は既定値。
    void ReadRect(const TerrainGrid& grid, const SampleRect& rect, std::vector<T>& out) const {
        out.assign(rect.Count(), defaultValue_);
        ForEachTileHolding(grid, rect, [&](TileCoord tile) {
            const TileRaster<T>* raster = Find(tile);
            if (raster == nullptr) {
                return;
            }
            const SampleRect part = IntersectRects(rect, TileSampleRect(grid, tile));
            CopyPart(*raster, tile, part, rect, out);
        });
    }

    // 矩形を書く。矩形の標本を持つ全タイルを作って同じ値を入れる（keep が false の位置は書かない）。
    void WriteRect(const TerrainGrid& grid, const SampleRect& rect, const std::vector<T>& values,
                   const std::vector<uint8_t>* keep = nullptr) {
        if (values.size() != rect.Count() || (keep != nullptr && keep->size() != rect.Count())) {
            return;
        }
        ForEachTileHolding(grid, rect, [&](TileCoord tile) {
            const SampleRect part = IntersectRects(rect, TileSampleRect(grid, tile));
            if (part.Empty()) {
                return;
            }
            if (keep != nullptr && !AnyKept(part, rect, *keep)) {
                return;
            }
            TileRaster<T>& raster = Ensure(tile);
            for (uint32_t z = 0u; z < part.height; ++z) {
                const int64_t globalZ = part.z0 + z;
                const std::size_t source = static_cast<std::size_t>(globalZ - rect.z0) * rect.width +
                                           static_cast<std::size_t>(part.x0 - rect.x0);
                const std::size_t target =
                    static_cast<std::size_t>(globalZ - TileFirstSample(grid, tile.z)) * samples_ +
                    static_cast<std::size_t>(part.x0 - TileFirstSample(grid, tile.x));
                for (uint32_t x = 0u; x < part.width; ++x) {
                    if (keep == nullptr || (*keep)[source + x] != 0u) {
                        raster.values[target + x] = values[source + x];
                    }
                }
            }
        });
    }

    bool operator==(const TiledRaster&) const = default;

private:
    void CopyPart(const TileRaster<T>& raster, TileCoord tile, const SampleRect& part, const SampleRect& rect,
                  std::vector<T>& out) const {
        const int64_t tileX0 = static_cast<int64_t>(tile.x) * cells_;
        const int64_t tileZ0 = static_cast<int64_t>(tile.z) * cells_;
        for (uint32_t z = 0u; z < part.height; ++z) {
            const int64_t globalZ = part.z0 + z;
            const std::size_t rectRow =
                static_cast<std::size_t>(globalZ - rect.z0) * rect.width + static_cast<std::size_t>(part.x0 - rect.x0);
            const std::size_t tileRow =
                static_cast<std::size_t>(globalZ - tileZ0) * samples_ + static_cast<std::size_t>(part.x0 - tileX0);
            for (uint32_t x = 0u; x < part.width; ++x) {
                out[rectRow + x] = raster.values[tileRow + x];
            }
        }
    }
    static bool AnyKept(const SampleRect& part, const SampleRect& rect, const std::vector<uint8_t>& keep) {
        for (uint32_t z = 0u; z < part.height; ++z) {
            const std::size_t row = static_cast<std::size_t>(part.z0 + z - rect.z0) * rect.width +
                                    static_cast<std::size_t>(part.x0 - rect.x0);
            for (uint32_t x = 0u; x < part.width; ++x) {
                if (keep[row + x] != 0u) {
                    return true;
                }
            }
        }
        return false;
    }

    uint32_t cells_ = 0u;
    uint32_t samples_ = 0u;
    T defaultValue_{};
    std::map<TileCoord, TileRaster<T>> tiles_{};
};

} // namespace HIKARI::TERRAIN::DATA
