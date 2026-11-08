#include "TerrainKit/Data/HIKARI_TerrainDataEdit.h"

#include <set>
#include <type_traits>
#include <utility>

#include "HIKARI_TerrainDataCodec.h"

namespace HIKARI::TERRAIN::DATA {
namespace {

// 鍵の画素の種類に合わせて全体配列を渡す。層が無ければ false。
template <class Fn>
bool VisitRaster(TerrainData& data, const TerrainTileKey& key, Fn&& fn) {
    switch (key.channel) {
    case TerrainChannel::LayerValues:
        if (TiledRaster<float>* raster = data.LayerValues(key.layerId)) {
            fn(*raster);
            return true;
        }
        return false;
    case TerrainChannel::LayerMask:
        if (TiledRaster<float>* raster = data.LayerMask(key.layerId)) {
            fn(*raster);
            return true;
        }
        return false;
    case TerrainChannel::Control: fn(data.Control()); return true;
    }
    return false;
}

template <class T>
void CaptureTile(const TiledRaster<T>& raster, TileCoord tile, bool& exists, std::vector<uint8_t>& bytes) {
    const TileRaster<T>* found = raster.Find(tile);
    exists = found != nullptr;
    bytes.clear();
    if (found != nullptr) {
        CODEC::Deflate(CODEC::AsBytes(found->values), bytes);
    }
}

// 縮めたバイトを展開する（適用はしない）。
template <class T>
bool DecodeTile(uint32_t samples, const std::vector<uint8_t>& bytes, TileRaster<T>& out) {
    const std::size_t count = static_cast<std::size_t>(samples) * samples;
    std::vector<uint8_t> raw;
    if (!CODEC::Inflate(bytes, count * sizeof(T), raw)) {
        return false;
    }
    out.samples = samples;
    return CODEC::FromBytes(raw, count, out.values);
}

struct StagedTile {
    const TerrainTileDelta* delta = nullptr;
    bool exists = false;
    TileRaster<float> floats{};
    TileRaster<uint32_t> control{};
};

bool LayerHolds(const HeightLayerStack& stack, const TerrainTileKey& key) {
    if (key.channel == TerrainChannel::Control) {
        return true;
    }
    for (const HeightLayer& layer : stack.layers) {
        if (layer.id == key.layerId) {
            return key.channel == TerrainChannel::LayerMask || layer.kind == HeightLayerKind::Sculpt;
        }
    }
    return false;
}

// 全タイルを展開して確かめてから適用する。途中で失敗しても data は変わらない。
bool ApplySide(TerrainData& data, const TerrainEdit& edit, bool before, std::string& error) {
    const std::optional<HeightLayerStack>& stack = before ? edit.stackBefore : edit.stackAfter;
    if (stack.has_value() && !ValidateHeightLayerStack(*stack, &error)) {
        return false;
    }
    const HeightLayerStack& target = stack.has_value() ? *stack : data.Stack();
    const uint32_t samples = TileSamples(target.grid);
    std::vector<StagedTile> staged;
    staged.reserve(edit.tiles.size());
    for (const TerrainTileDelta& delta : edit.tiles) {
        StagedTile tile{};
        tile.delta = &delta;
        tile.exists = before ? delta.existedBefore : delta.existsAfter;
        if (tile.exists) {
            const std::vector<uint8_t>& bytes = before ? delta.before : delta.after;
            const bool decoded = delta.key.channel == TerrainChannel::Control ? DecodeTile(samples, bytes, tile.control)
                                                                              : DecodeTile(samples, bytes, tile.floats);
            if (!decoded) {
                error = "terrain edit tile data is corrupt";
                return false;
            }
            if (!LayerHolds(target, delta.key)) {
                error = "terrain edit refers to a layer that does not exist";
                return false;
            }
        }
        staged.push_back(std::move(tile));
    }
    // 先に層スタックを戻し（層の画素の入れ物ができる）、それからタイルを戻す。
    if (stack.has_value() && !data.ReplaceStack(*stack, &error)) {
        return false;
    }
    for (StagedTile& tile : staged) {
        const TerrainTileKey& key = tile.delta->key;
        VisitRaster(data, key, [&](auto& raster) {
            if (!tile.exists) {
                raster.Erase(key.tile);
            } else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(raster)>, TiledRaster<uint32_t>>) {
                raster.Assign(key.tile, std::move(tile.control));
            } else {
                raster.Assign(key.tile, std::move(tile.floats));
            }
        });
    }
    data.MarkChanged();
    return true;
}

} // namespace

std::size_t TerrainEdit::ByteSize() const noexcept {
    std::size_t bytes = label.size();
    for (const TerrainTileDelta& delta : tiles) {
        bytes += sizeof(TerrainTileDelta) + delta.before.size() + delta.after.size();
    }
    return bytes;
}

std::vector<TileCoord> TerrainEdit::ChangedTiles() const {
    std::set<TileCoord> unique;
    for (const TerrainTileDelta& delta : tiles) {
        unique.insert(delta.key.tile);
    }
    return {unique.begin(), unique.end()};
}

TerrainEditRecorder::TerrainEditRecorder(TerrainData& data) : data_(data), stackBefore_(data.Stack()) {}

void TerrainEditRecorder::Touch(const TerrainTileKey& key) {
    if (pending_.contains(key)) {
        return;
    }
    Pending pending{};
    if (!VisitRaster(data_, key,
                     [&](auto& raster) { CaptureTile(raster, key.tile, pending.existed, pending.before); })) {
        return;
    }
    pending_.emplace(key, std::move(pending));
}

void TerrainEditRecorder::TouchRect(TerrainChannel channel, uint32_t layerId, const SampleRect& rect) {
    ForEachTileHolding(data_.Grid(), rect, [&](TileCoord tile) { Touch(TerrainTileKey{channel, layerId, tile}); });
}

void TerrainEditRecorder::TouchAll(TerrainChannel channel, uint32_t layerId) {
    std::vector<TileCoord> tiles;
    VisitRaster(data_, TerrainTileKey{channel, layerId, {}}, [&](auto& raster) {
        for (const auto& entry : raster.Tiles()) {
            tiles.push_back(entry.first);
        }
    });
    for (TileCoord tile : tiles) {
        Touch(TerrainTileKey{channel, layerId, tile});
    }
}

TerrainEdit TerrainEditRecorder::Finish(std::string label) {
    TerrainEdit edit{};
    edit.label = std::move(label);
    for (auto& [key, pending] : pending_) {
        TerrainTileDelta delta{};
        delta.key = key;
        delta.existedBefore = pending.existed;
        delta.before = std::move(pending.before);
        // 層が消えていれば「後は無い」。
        VisitRaster(data_, key, [&](auto& raster) { CaptureTile(raster, key.tile, delta.existsAfter, delta.after); });
        if (delta.existedBefore == delta.existsAfter && delta.before == delta.after) {
            continue;
        }
        edit.tiles.push_back(std::move(delta));
    }
    if (data_.Stack() != stackBefore_) {
        edit.stackBefore = stackBefore_;
        edit.stackAfter = data_.Stack();
    }
    pending_.clear();
    stackBefore_ = data_.Stack();
    return edit;
}

bool UndoTerrainEdit(TerrainData& data, const TerrainEdit& edit, std::string& error) {
    return ApplySide(data, edit, true, error);
}

bool RedoTerrainEdit(TerrainData& data, const TerrainEdit& edit, std::string& error) {
    return ApplySide(data, edit, false, error);
}

} // namespace HIKARI::TERRAIN::DATA
