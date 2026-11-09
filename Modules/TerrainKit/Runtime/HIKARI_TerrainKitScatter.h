#pragma once

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "TerrainKit/Data/HIKARI_TerrainDataGrid.h"
#include "TerrainKit/Data/HIKARI_TerrainDataScatter.h"

// 散布の持ち主：規則（資産 .hscatter）、手で消した個体の種（場面の .cells）、結果を実例集へ書く処理。
// 計算は TerrainRuleBaker が背景で材質規則と一緒に行う。規則の結果は決まった種を持つので、手で消した種を外し、
// 手で置いた個体（HandPlaced）は触らずに差し替える。出た結果は実例集のファイルへ保存され、実行時は読むだけ。
namespace HIKARI::TERRAINKIT {

class TerrainStore;

inline constexpr double kInstanceCellMeters = 256.0; // 引擎の既定の格子（WorldCellAt と同じ）

inline int32_t InstanceCellIndex(double value) noexcept {
    return static_cast<int32_t>(std::floor(value / kInstanceCellMeters));
}

class TerrainScatter {
public:
    void Load(const std::filesystem::path& cellDirectory, const std::filesystem::path& projectRoot);
    // force なら変わっていなくても書く（別名で保存）。
    bool Save(const std::filesystem::path& cellDirectory, const std::filesystem::path& projectRoot, bool force,
              std::string& error);
    void Clear();
    bool HasUnsaved() const noexcept { return rulesDirty_ || sceneDirty_; }
    // 規則を使っている場面か。使っていなければ計算も差し替えもしない（他の手段で書いた個体を消さない）。
    bool Active() const noexcept {
        return !rules_.rules.empty() || !rules_.lines.empty() || rulesDirty_ || !assetPath_.empty();
    }

    const TERRAIN::DATA::ScatterRuleSet& Rules() const noexcept { return rules_; }
    // 保存されている個体を作った入力（散布規則と地表規則）の指紋。開いた時に今の規則と比べる。
    uint64_t BakedInputs() const noexcept { return bakedInputs_; }
    void SetBakedInputs(uint64_t inputs) noexcept;
    const std::string& AssetPath() const noexcept { return assetPath_; }
    void SetRules(TERRAIN::DATA::ScatterRuleSet rules);
    // 手で消した規則の個体。規則を計算し直しても戻さない。
    void AddExcluded(std::span<const uint32_t> seeds);
    void RemoveExcluded(std::span<const uint32_t> seeds);

    // 手で置いた個体を、規則が避ける場所として集める。
    std::vector<TERRAIN::DATA::ScatterReserved> CollectReserved(const TerrainStore& store,
                                                                TERRAIN::DATA::TileCoord tile) const;
    // 瓦片の中の規則の個体を結果で差し替える。rules は計算に使った規則（名前から実例集を引く）。
    void Apply(const TerrainStore& store, const TERRAIN::DATA::ScatterRuleSet& rules, TERRAIN::DATA::TileCoord tile,
               std::span<const TERRAIN::DATA::ScatterPoint> points) const;

private:
    TERRAIN::DATA::ScatterRuleSet rules_{};
    std::string assetPath_{};               // プロジェクトの根からの相対
    std::vector<uint32_t> excludedSeeds_{}; // 昇順
    uint64_t bakedInputs_ = 0u;
    bool rulesDirty_ = false;
    bool sceneDirty_ = false;
};

} // namespace HIKARI::TERRAINKIT
