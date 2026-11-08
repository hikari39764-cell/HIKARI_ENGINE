#include "TerrainKit/Data/HIKARI_TerrainData.h"

#include <algorithm>
#include <set>
#include <utility>

namespace HIKARI::TERRAIN::DATA {

TerrainData::TerrainData(const TerrainGrid& grid) : control_(grid, 0u) { stack_.grid = grid; }

TerrainData::LayerData TerrainData::MakeLayerData() const {
    LayerData data{};
    data.values = TiledRaster<float>(stack_.grid, 0.0f);
    data.mask = TiledRaster<float>(stack_.grid, 1.0f);
    return data;
}

const HeightLayer* TerrainData::FindLayer(uint32_t id) const noexcept {
    const auto it = std::find_if(stack_.layers.begin(), stack_.layers.end(),
                                 [id](const HeightLayer& layer) { return layer.id == id; });
    return it != stack_.layers.end() ? &*it : nullptr;
}

std::size_t TerrainData::LayerIndex(uint32_t id) const noexcept {
    for (std::size_t index = 0u; index < stack_.layers.size(); ++index) {
        if (stack_.layers[index].id == id) {
            return index;
        }
    }
    return kAppend;
}

uint32_t TerrainData::AddLayer(HeightLayer layer, std::size_t index) {
    layer = JSONFIELDS::Sanitize(std::move(layer), kHeightLayerFields);
    layer.id = stack_.nextLayerId;
    HeightLayerStack candidate = stack_;
    candidate.nextLayerId = layer.id + 1u;
    const std::size_t at = (std::min)(index, candidate.layers.size());
    candidate.layers.insert(candidate.layers.begin() + static_cast<std::ptrdiff_t>(at), layer);
    if (!ValidateHeightLayerStack(candidate)) {
        return 0u;
    }
    stack_ = std::move(candidate);
    layerData_[layer.id] = MakeLayerData();
    MarkChanged();
    return layer.id;
}

bool TerrainData::RemoveLayer(uint32_t id) {
    const std::size_t index = LayerIndex(id);
    if (index == kAppend) {
        return false;
    }
    stack_.layers.erase(stack_.layers.begin() + static_cast<std::ptrdiff_t>(index));
    layerData_.erase(id);
    MarkChanged();
    return true;
}

bool TerrainData::MoveLayer(uint32_t id, std::size_t index) {
    const std::size_t from = LayerIndex(id);
    if (from == kAppend || index >= stack_.layers.size()) {
        return false;
    }
    if (from != index) {
        HeightLayer layer = std::move(stack_.layers[from]);
        stack_.layers.erase(stack_.layers.begin() + static_cast<std::ptrdiff_t>(from));
        stack_.layers.insert(stack_.layers.begin() + static_cast<std::ptrdiff_t>(index), std::move(layer));
        MarkChanged();
    }
    return true;
}

bool TerrainData::SetLayer(const HeightLayer& layer) {
    const std::size_t index = LayerIndex(layer.id);
    if (index == kAppend || stack_.layers[index].kind != layer.kind) {
        return false;
    }
    HeightLayer sanitized = JSONFIELDS::Sanitize(layer, kHeightLayerFields);
    sanitized.id = layer.id;
    sanitized.kind = layer.kind;
    if (sanitized == stack_.layers[index]) {
        return true;
    }
    if (sanitized.kind == HeightLayerKind::Image && sanitized.image.source != stack_.layers[index].image.source) {
        layerData_[layer.id].image.reset();
    }
    stack_.layers[index] = std::move(sanitized);
    MarkChanged();
    return true;
}

bool TerrainData::ReplaceStack(const HeightLayerStack& stack, std::string* error) {
    if (!ValidateHeightLayerStack(stack, error)) {
        return false;
    }
    const bool gridChanged = stack.grid != stack_.grid;
    std::set<uint32_t> keep;
    for (const HeightLayer& layer : stack.layers) {
        keep.insert(layer.id);
    }
    if (gridChanged) {
        layerData_.clear();
        control_ = TiledRaster<uint32_t>(stack.grid, 0u);
    } else {
        std::erase_if(layerData_, [&](const auto& entry) { return !keep.contains(entry.first); });
    }
    stack_ = stack;
    for (const HeightLayer& layer : stack_.layers) {
        if (!layerData_.contains(layer.id)) {
            layerData_[layer.id] = MakeLayerData();
        }
        if (layer.kind != HeightLayerKind::Sculpt) {
            layerData_[layer.id].values.Clear();
        }
    }
    MarkChanged();
    return true;
}

TiledRaster<float>* TerrainData::LayerValues(uint32_t id) noexcept {
    return const_cast<TiledRaster<float>*>(std::as_const(*this).LayerValues(id));
}

const TiledRaster<float>* TerrainData::LayerValues(uint32_t id) const noexcept {
    const HeightLayer* layer = FindLayer(id);
    const auto it = layerData_.find(id);
    if (layer == nullptr || layer->kind != HeightLayerKind::Sculpt || it == layerData_.end()) {
        return nullptr;
    }
    return &it->second.values;
}

TiledRaster<float>* TerrainData::LayerMask(uint32_t id) noexcept {
    return const_cast<TiledRaster<float>*>(std::as_const(*this).LayerMask(id));
}

const TiledRaster<float>* TerrainData::LayerMask(uint32_t id) const noexcept {
    const auto it = layerData_.find(id);
    return FindLayer(id) != nullptr && it != layerData_.end() ? &it->second.mask : nullptr;
}

bool TerrainData::SetLayerImage(uint32_t id, std::shared_ptr<const HeightImage> image) {
    const HeightLayer* layer = FindLayer(id);
    if (layer == nullptr || layer->kind != HeightLayerKind::Image || (image != nullptr && !image->Valid())) {
        return false;
    }
    layerData_[id].image = std::move(image);
    MarkChanged();
    return true;
}

std::shared_ptr<const HeightImage> TerrainData::LayerImage(uint32_t id) const noexcept {
    const auto it = layerData_.find(id);
    return it != layerData_.end() ? it->second.image : nullptr;
}

std::vector<TileCoord> TerrainData::AuthoredTiles() const {
    std::set<TileCoord> tiles;
    for (const auto& [id, data] : layerData_) {
        for (const auto& [tile, raster] : data.values.Tiles()) {
            tiles.insert(tile);
        }
        for (const auto& [tile, raster] : data.mask.Tiles()) {
            tiles.insert(tile);
        }
    }
    for (const auto& [tile, raster] : control_.Tiles()) {
        tiles.insert(tile);
    }
    return {tiles.begin(), tiles.end()};
}

} // namespace HIKARI::TERRAIN::DATA
