#pragma once

#include <array>
#include <cstdint>
#include <future>
#include <set>
#include <vector>

#include "TerrainKit/Data/HIKARI_TerrainDataGrid.h"

// 自動材質規則と遠景の色を背景で瓦片ごとに計算する。編集を止めない：入力は写しで渡し、結果は描画の thread で受け取る。
namespace HIKARI::TERRAINKIT {

class TerrainStore;

inline constexpr uint32_t kFarCacheSize = 129u;
inline constexpr uint32_t kFarCacheMips = 8u;

// 制御図の標本の色：2 材質の平均色を混ぜる（colors は枠ごとの RGB + 粗さ）。遠景の色と光線追跡の代理が共有する。
std::array<float, 4> ControlSampleColor(uint32_t control, const std::vector<std::array<float, 4>>& colors) noexcept;
// 材質の表（枠ごとの RGB + 粗さ）。
std::vector<std::array<float, 4>> SurfaceColorTable(const TerrainStore& store);

class TerrainRuleBaker {
public:
    void MarkDirty(TERRAIN::DATA::TileCoord tile);
    void MarkAllDirty(const TerrainStore& store);
    // 描画の thread で毎フレーム：終わった分を受け取り、次の分を始める。
    void Update(TerrainStore& store);
    // 背景の計算を待って結果を捨て、待っている瓦片も忘れる（データを替える時と DLL を外す前）。
    void Cancel();
    size_t PendingTiles() const noexcept { return dirty_.size() + activeTiles_; }

private:
    struct TileResult {
        TERRAIN::DATA::TileCoord tile{};
        std::vector<uint32_t> control{};
        std::vector<uint8_t> farColor{};
    };

    std::set<TERRAIN::DATA::TileCoord> dirty_{};
    std::future<std::vector<TileResult>> job_{};
    size_t activeTiles_ = 0u;
    uint32_t quietFrames_ = 0u;
};

} // namespace HIKARI::TERRAINKIT
