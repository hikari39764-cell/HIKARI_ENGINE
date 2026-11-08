#pragma once

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

class TerrainRuleBaker {
public:
    void MarkDirty(TERRAIN::DATA::TileCoord tile);
    void MarkAllDirty(const TerrainStore& store);
    // 描画の thread で毎フレーム：終わった分を受け取り、次の分を始める。
    void Update(TerrainStore& store);
    // 背景の計算を待つ。DLL を外す前に呼ぶ。
    void Shutdown();
    size_t PendingTiles() const noexcept { return dirty_.size() + activeTiles_; }

private:
    struct TileResult {
        TERRAIN::DATA::TileCoord tile{};
        bool hasControl = false;
        std::vector<uint32_t> control{};
        std::vector<uint8_t> farColor{};
    };

    std::set<TERRAIN::DATA::TileCoord> dirty_{};
    std::future<std::vector<TileResult>> job_{};
    size_t activeTiles_ = 0u;
    uint32_t quietFrames_ = 0u;
};

} // namespace HIKARI::TERRAINKIT
