#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "TerrainKit/Data/HIKARI_TerrainDataImage.h"
#include "TerrainKit/Data/HIKARI_TerrainDataLayers.h"
#include "TerrainKit/Data/HIKARI_TerrainDataRaster.h"

// 一つの地形の編集データ全体：層スタックの記述、層ごとの画素（手彫りの値・マスク）、材質の制御図、読み込んだ高さ画像。
// 焼いた高さ（16 bit）は持たず、HeightStackEvaluator が必要なタイルだけ求める。
namespace HIKARI::TERRAIN::DATA {

// タイル単位で記録・保存する画素の種類。
enum class TerrainChannel : uint8_t {
    LayerValues = 0, // 手彫り層の値（float、既定 0）
    LayerMask = 1,   // 層のマスク（float 0..1、既定 1）
    Control = 2,     // 材質の制御図（uint32、既定 0）
};

class TerrainData {
public:
    static constexpr std::size_t kAppend = (std::numeric_limits<std::size_t>::max)();

    explicit TerrainData(const TerrainGrid& grid = {});

    const TerrainGrid& Grid() const noexcept { return stack_.grid; }
    const HeightLayerStack& Stack() const noexcept { return stack_; }
    const HeightLayer* FindLayer(uint32_t id) const noexcept;
    // 層の位置（下から数える）。無ければ kAppend。
    std::size_t LayerIndex(uint32_t id) const noexcept;

    // 層を足して ID を返す（設定は丸める）。index は下から数えた挿入位置。失敗は 0。
    uint32_t AddLayer(HeightLayer layer, std::size_t index = kAppend);
    // 層と、その画素・画像を消す。
    bool RemoveLayer(uint32_t id);
    bool MoveLayer(uint32_t id, std::size_t index);
    // 同じ ID・同じ種類の層の設定を差し替える（丸める）。
    bool SetLayer(const HeightLayer& layer);
    // JSON から読んだスタックへ丸ごと替える。格子が変わる時は画素を全て消す。
    bool ReplaceStack(const HeightLayerStack& stack, std::string* error = nullptr);

    // 手彫り層の値（既定 0）。層が無いか手彫りでなければ nullptr。
    TiledRaster<float>* LayerValues(uint32_t id) noexcept;
    const TiledRaster<float>* LayerValues(uint32_t id) const noexcept;
    // 層のマスク（既定 1）。層が無ければ nullptr。
    TiledRaster<float>* LayerMask(uint32_t id) noexcept;
    const TiledRaster<float>* LayerMask(uint32_t id) const noexcept;
    TiledRaster<uint32_t>& Control() noexcept { return control_; }
    const TiledRaster<uint32_t>& Control() const noexcept { return control_; }

    // 画像層へ読み込んだ高さ画像を付ける（読み込みは呼び出し側。パスは層の設定が持つ）。
    bool SetLayerImage(uint32_t id, std::shared_ptr<const HeightImage> image);
    std::shared_ptr<const HeightImage> LayerImage(uint32_t id) const noexcept;

    // 画素を 1 タイルでも持つタイル（手彫り・マスク・制御図の和）。
    std::vector<TileCoord> AuthoredTiles() const;
    // 変更の通し番号（層の設定・画素・画像のどれかが変わるたびに増える）。評価器の作り直しの判定に使う。
    uint64_t Revision() const noexcept { return revision_; }
    void MarkChanged() noexcept { ++revision_; }

private:
    struct LayerData {
        TiledRaster<float> values{};
        TiledRaster<float> mask{};
        std::shared_ptr<const HeightImage> image{};
    };

    LayerData MakeLayerData() const;

    HeightLayerStack stack_{};
    std::map<uint32_t, LayerData> layerData_{};
    TiledRaster<uint32_t> control_{};
    uint64_t revision_ = 1u;
};

// 層スタックの評価。作った時点の層設定から噪声の生成器を組み、画素と画像は TerrainData を参照する
// （評価中に TerrainData を変えない）。複数スレッドから同時に評価してよい。
class HeightStackEvaluator {
public:
    explicit HeightStackEvaluator(const TerrainData& data);
    ~HeightStackEvaluator();
    HeightStackEvaluator(const HeightStackEvaluator&) = delete;
    HeightStackEvaluator& operator=(const HeightStackEvaluator&) = delete;

    // 作る時に噪声の生成器を組めたか（符号化済みノード木が壊れている時など false）。
    bool Valid() const noexcept;
    const std::string& Error() const noexcept;

    // 矩形の高さ（メートル）。throughLayer を指定するとその層まで（その層を含む）で止める。
    bool EvaluateRect(const SampleRect& rect, std::vector<float>& out, std::string* error = nullptr,
                      uint32_t throughLayer = 0u) const;
    bool EvaluateTile(TileCoord tile, std::vector<float>& out, std::string* error = nullptr) const;
    // 焼いた 16 bit 高さのタイル。
    bool BakeTile(TileCoord tile, TileRaster<uint16_t>& out, std::string* error = nullptr) const;
    // 一つの層だけの値（合成前）。
    bool EvaluateLayerValues(uint32_t layerId, const SampleRect& rect, std::vector<float>& values,
                             std::vector<float>& alpha, std::string* error = nullptr) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace HIKARI::TERRAIN::DATA
