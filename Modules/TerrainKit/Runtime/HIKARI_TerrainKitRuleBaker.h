#pragma once

#include <cstdint>
#include <future>
#include <memory>
#include <set>
#include <vector>

#include "TerrainKit/Data/HIKARI_TerrainDataAnalysis.h"
#include "TerrainKit/Data/HIKARI_TerrainDataGrid.h"
#include "TerrainKit/Data/HIKARI_TerrainDataScatter.h"

// 自動材質規則と色の図、散布を背景で瓦片ごとに計算する。編集を止めない：入力は写しで渡し、結果は描画の thread で受け取る。
// 開いた時や描画の資源を作り直した時は色の図だけ作り直す。散布は結果をファイルへ保存してあるので、編集の時だけ解く。
namespace HIKARI::TERRAINKIT {

class TerrainStore;

inline constexpr uint32_t kColorMapSize = 257u;
inline constexpr uint32_t kColorMapMips = 9u;

class TerrainRuleBaker {
public:
    void MarkDirty(TERRAIN::DATA::TileCoord tile);
    void MarkAllDirty(const TerrainStore& store);
    // 高さ・材質・規則を編集した時：散布も計算し直す。
    void MarkScatterDirty(TERRAIN::DATA::TileCoord tile);
    void MarkAllScatterDirty(const TerrainStore& store);
    // 描画の thread で毎フレーム：終わった分を受け取り、次の分を始める。
    void Update(TerrainStore& store);
    // 背景の計算を待って結果を捨て、待っている瓦片も忘れる（データを替える時と DLL を外す前）。
    void Cancel();
    size_t PendingTiles() const noexcept { return dirty_.size() + activeTiles_; }

private:
    struct TileResult {
        TERRAIN::DATA::TileCoord tile{};
        std::vector<uint32_t> control{};
        std::vector<uint8_t> colorMap{};
        bool scattered = false;
        std::vector<TERRAIN::DATA::ScatterPoint> points{};
    };

    struct JobResult {
        std::vector<TileResult> tiles{};
        std::shared_ptr<const TERRAIN::DATA::TerrainAnalysis> analysis{};
        std::shared_ptr<const TERRAIN::DATA::ScatterRuleSet> scatterRules{};
    };

    std::set<TERRAIN::DATA::TileCoord> dirty_{};
    std::set<TERRAIN::DATA::TileCoord> scatterDirty_{};
    std::future<JobResult> job_{};
    std::shared_ptr<const TERRAIN::DATA::TerrainAnalysis> analysis_{};
    size_t activeTiles_ = 0u;
    uint32_t quietFrames_ = 0u;
};

} // namespace HIKARI::TERRAINKIT
