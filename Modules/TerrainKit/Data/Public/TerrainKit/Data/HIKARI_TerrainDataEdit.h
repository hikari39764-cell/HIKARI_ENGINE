#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "TerrainKit/Data/HIKARI_TerrainData.h"

// タイル単位の撤销記録。操作の前に触るタイルを Touch し、終わったら Finish で前後の差を 1 件にまとめる。
// 前後の画素は deflate で縮めて持ち、変わらなかったタイルは落とす。層スタックの記述が変わった時は前後の記述も持つ。
namespace HIKARI::TERRAIN::DATA {

struct TerrainTileKey {
    TerrainChannel channel = TerrainChannel::LayerValues;
    uint32_t layerId = 0u; // 制御図は 0
    TileCoord tile{};
    auto operator<=>(const TerrainTileKey&) const = default;
};

struct TerrainTileDelta {
    TerrainTileKey key{};
    bool existedBefore = false;
    bool existsAfter = false;
    std::vector<uint8_t> before{}; // 縮めた生バイト（無かったタイルは空）
    std::vector<uint8_t> after{};
};

struct TerrainEdit {
    std::string label{};
    std::vector<TerrainTileDelta> tiles{};
    std::optional<HeightLayerStack> stackBefore{};
    std::optional<HeightLayerStack> stackAfter{};

    bool Empty() const noexcept { return tiles.empty() && !stackBefore.has_value(); }
    std::size_t ByteSize() const noexcept;
    // 変わったタイル（重複なし、x、z の順）。
    std::vector<TileCoord> ChangedTiles() const;
};

class TerrainEditRecorder {
public:
    // 作った時点の層スタックを覚える。
    explicit TerrainEditRecorder(TerrainData& data);

    // 変更前の状態を一度だけ取る。
    void Touch(const TerrainTileKey& key);
    // 矩形の標本を持つ全タイル。
    void TouchRect(TerrainChannel channel, uint32_t layerId, const SampleRect& rect);
    // その層（または制御図）の今ある全タイル。層を消す前に呼ぶ。
    void TouchAll(TerrainChannel channel, uint32_t layerId);
    // 変更後を取り、変わらなかったタイルを落として 1 件にまとめる。記録器は空に戻る。
    TerrainEdit Finish(std::string label);

private:
    struct Pending {
        bool existed = false;
        std::vector<uint8_t> before{};
    };

    TerrainData& data_;
    HeightLayerStack stackBefore_{};
    std::map<TerrainTileKey, Pending> pending_{};
};

// 撤销（before を戻す）と重做（after を戻す）。失敗時は data を変えない。
bool UndoTerrainEdit(TerrainData& data, const TerrainEdit& edit, std::string& error);
bool RedoTerrainEdit(TerrainData& data, const TerrainEdit& edit, std::string& error);

} // namespace HIKARI::TERRAIN::DATA
