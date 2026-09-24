#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"
#include "Scene/Terrain/HIKARI_SurfaceLayerRegistry.h"
#include "Scene/HIKARI_GameObject.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace HIKARI {
namespace {
nlohmann::json SerializeTerrainChunkDeltaEntry(const TERRAIN::TerrainChunkKey& key,
                                               const std::optional<TERRAIN::TerrainChunk>& chunk) {
    nlohmann::json entry{
        {"x", key.x},
        {"z", key.z},
        {"exists", chunk.has_value()},
    };
    if (!chunk.has_value()) {
        return entry;
    }
    entry["cells"] = chunk->cells;
    entry["layerCount"] = chunk->layerCount;
    entry["heights"] = chunk->heights;
    entry["coverage"] = chunk->coverage;
    entry["layerWeights"] = chunk->layerWeights;
    return entry;
}

} // namespace

TERRAIN::TerrainChunk& TerrainSurfaceComponent::EnsureChunk(const TERRAIN::TerrainChunkKey& key, int32_t seedLayer) {
    auto it = surface_.chunks.find(key);
    if (it == surface_.chunks.end()) {
        CaptureChunkBeforeEdit(key);
        it = surface_.chunks.emplace(key, TERRAIN::MakeTerrainChunk(GetChunkCells(), GetLayerCount())).first;
        std::fill(it->second.coverage.begin(), it->second.coverage.end(), 0.0f);
        if (seedLayer >= 0) {
            (void)TERRAIN::FillTerrainChunkLayer(it->second, static_cast<uint32_t>(seedLayer));
        } else {
            if (!SeedChunkFromNeighborEdges(key, it->second)) {
                // 隣接 chunk が無い場合も無条件に Sand (layer 0) へ
                // 戻さず、authored の solid preset を fallback にする。
                // 直前に Grass / Rock を全体 fill していれば、孤立した
                // 追加 chunk も同じ材質を引き継ぐ。
                int baseLayer = TERRAIN::SurfaceLayerRegistry::Get().ResolveIndex(surface_.presetId);
                if (baseLayer < 0) {
                    baseLayer = 0;
                }
                (void)TERRAIN::FillTerrainChunkLayer(it->second, static_cast<uint32_t>(baseLayer));
            }
        }
        MarkCoverageChunkDirty(key);
        MarkChunkChanged(key);
    }
    return it->second;
}

bool TerrainSurfaceComponent::SeedChunkFromNeighborEdges(const TERRAIN::TerrainChunkKey& key,
                                                         TERRAIN::TerrainChunk& chunk) {
    struct Candidate {
        TERRAIN::TerrainChunkKey key{};
        TERRAIN::TerrainChunkEdge edge = TERRAIN::TerrainChunkEdge::NegativeX;
    };
    // 角の順序は固定する。既存の cardinal neighbour を constructor の
    // 基底層より優先し、隣接が無い場合だけ MakeTerrainChunk の layer 0
    // を fallback とする。
    const Candidate candidates[] = {
        {{key.x - 1, key.z}, TERRAIN::TerrainChunkEdge::PositiveX},
        {{key.x + 1, key.z}, TERRAIN::TerrainChunkEdge::NegativeX},
        {{key.x, key.z - 1}, TERRAIN::TerrainChunkEdge::PositiveZ},
        {{key.x, key.z + 1}, TERRAIN::TerrainChunkEdge::NegativeZ},
    };
    for (const Candidate& candidate : candidates) {
        const auto found = surface_.chunks.find(candidate.key);
        if (found == surface_.chunks.end() || !TERRAIN::TerrainChunkValid(found->second)) {
            continue;
        }
        if (TERRAIN::CopyTerrainChunkEdgeToAll(found->second, chunk, candidate.edge)) {
            return true;
        }
    }

    // Brush が複数 chunk の疎な gap をまたぐ場合は最寄りの authored
    // chunk まで探索する。gap 全体を黙って Sand / base に戻さない。
    // 同距離は map 順で解決し、Editor session 間の決定性を保つ。
    const TERRAIN::TerrainChunk* nearest = nullptr;
    TERRAIN::TerrainChunkEdge nearestEdge = TERRAIN::TerrainChunkEdge::NegativeX;
    int64_t nearestDistance = (std::numeric_limits<int64_t>::max)();
    for (const auto& [candidateKey, candidateChunk] : surface_.chunks) {
        const int64_t dx = static_cast<int64_t>(candidateKey.x) - static_cast<int64_t>(key.x);
        const int64_t dz = static_cast<int64_t>(candidateKey.z) - static_cast<int64_t>(key.z);
        if (dx == 0 && dz == 0 || !TERRAIN::TerrainChunkValid(candidateChunk)) {
            continue;
        }
        const int64_t absDx = dx < 0 ? -dx : dx;
        const int64_t absDz = dz < 0 ? -dz : dz;
        const int64_t distance = absDx + absDz;
        if (distance >= nearestDistance) {
            continue;
        }
        nearest = &candidateChunk;
        nearestDistance = distance;
        if (absDx >= absDz) {
            nearestEdge = dx < 0 ? TERRAIN::TerrainChunkEdge::PositiveX : TERRAIN::TerrainChunkEdge::NegativeX;
        } else {
            nearestEdge = dz < 0 ? TERRAIN::TerrainChunkEdge::PositiveZ : TERRAIN::TerrainChunkEdge::NegativeZ;
        }
    }
    if (nearest != nullptr && TERRAIN::CopyTerrainChunkEdgeToAll(*nearest, chunk, nearestEdge)) {
        return true;
    }
    return false;
}

bool TerrainSurfaceComponent::StitchChunkBorders(const std::vector<TERRAIN::TerrainChunkKey>& touchedChunks,
                                                 std::set<TERRAIN::TerrainChunkKey>& changedChunks,
                                                 bool* outHeightChanged, bool* outLayersChanged) {
    if (outHeightChanged != nullptr) {
        *outHeightChanged = false;
    }
    if (outLayersChanged != nullptr) {
        *outLayersChanged = false;
    }
    if (touchedChunks.empty() || surface_.chunks.empty()) {
        return false;
    }

    // 筆が触れた chunk と 8 近傍だけを seam island とする。共有頂点を
    // 書き戻す両側を必ず含めつつ、遠方 chunk の境界走査を避ける。
    std::set<TERRAIN::TerrainChunkKey> affected{};
    for (const TERRAIN::TerrainChunkKey& touched : touchedChunks) {
        for (int32_t dz = -1; dz <= 1; ++dz) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                const int64_t candidateX = static_cast<int64_t>(touched.x) + dx;
                const int64_t candidateZ = static_cast<int64_t>(touched.z) + dz;
                if (candidateX < (std::numeric_limits<int32_t>::min)() ||
                    candidateX > (std::numeric_limits<int32_t>::max)() ||
                    candidateZ < (std::numeric_limits<int32_t>::min)() ||
                    candidateZ > (std::numeric_limits<int32_t>::max)()) {
                    continue;
                }
                const TERRAIN::TerrainChunkKey candidate{static_cast<int32_t>(candidateX),
                                                         static_cast<int32_t>(candidateZ)};
                if (surface_.chunks.contains(candidate)) {
                    affected.insert(candidate);
                }
            }
        }
    }
    if (affected.empty()) {
        return false;
    }

    // Seam normalization may update either side of a border. Capture the
    // entire local seam island before the first write so terrain Undo is
    // complete without copying unrelated chunks.
    for (const TERRAIN::TerrainChunkKey& key : affected) {
        CaptureChunkBeforeEdit(key);
    }

    bool coverageChanged = false;

    uint32_t layerCount = GetLayerCount();
    for (const TERRAIN::TerrainChunkKey& key : affected) {
        const TERRAIN::TerrainChunk& chunk = surface_.chunks.at(key);
        layerCount = (std::max)(layerCount, chunk.layerCount);
    }
    layerCount = std::clamp(layerCount, 1u, TERRAIN::kTerrainChunkMaxLayers);

    for (const TERRAIN::TerrainChunkKey& key : affected) {
        TERRAIN::TerrainChunk& chunk = surface_.chunks.at(key);
        if (chunk.layerCount == layerCount) {
            continue;
        }
        const std::size_t count = TERRAIN::TerrainChunkVertexCount(chunk.cells);
        std::vector<float> expanded(count * static_cast<std::size_t>(layerCount), 0.0f);
        for (uint32_t layer = 0u; layer < (std::min)(chunk.layerCount, layerCount); ++layer) {
            std::copy_n(chunk.layerWeights.begin() + static_cast<std::size_t>(layer) * count, count,
                        expanded.begin() + static_cast<std::size_t>(layer) * count);
        }
        chunk.layerCount = layerCount;
        chunk.layerWeights = std::move(expanded);
        changedChunks.insert(key);
        if (outLayersChanged != nullptr) {
            *outLayersChanged = true;
        }
    }

    struct BorderValue {
        double height = 0.0;
        double coverage = 0.0;
        std::array<double, TERRAIN::kTerrainChunkMaxLayers> weights{};
        uint32_t count = 0u;
    };
    using VertexKey = std::pair<int64_t, int64_t>;
    std::map<VertexKey, BorderValue> values{};
    for (const TERRAIN::TerrainChunkKey& key : affected) {
        const TERRAIN::TerrainChunk& chunk = surface_.chunks.at(key);
        const uint32_t cells = chunk.cells;
        const uint32_t stride = cells + 1u;
        const std::size_t vertexCount = TERRAIN::TerrainChunkVertexCount(cells);
        for (uint32_t z = 0u; z <= cells; ++z) {
            for (uint32_t x = 0u; x <= cells; ++x) {
                if (x != 0u && x != cells && z != 0u && z != cells) {
                    continue;
                }
                const std::size_t index = static_cast<std::size_t>(z) * stride + x;
                const VertexKey vertex{static_cast<int64_t>(key.x) * cells + x,
                                       static_cast<int64_t>(key.z) * cells + z};
                BorderValue& value = values[vertex];
                value.height += chunk.heights[index];
                value.coverage += chunk.coverage[index];
                for (uint32_t layer = 0u; layer < layerCount; ++layer) {
                    value.weights[layer] += chunk.layerWeights[static_cast<std::size_t>(layer) * vertexCount + index];
                }
                ++value.count;
            }
        }
    }

    for (const TERRAIN::TerrainChunkKey& key : affected) {
        TERRAIN::TerrainChunk& chunk = surface_.chunks.at(key);
        const uint32_t cells = chunk.cells;
        const uint32_t stride = cells + 1u;
        const std::size_t vertexCount = TERRAIN::TerrainChunkVertexCount(cells);
        bool chunkHeightChanged = false;
        bool chunkCoverageChanged = false;
        bool chunkLayersChanged = false;
        for (uint32_t z = 0u; z <= cells; ++z) {
            for (uint32_t x = 0u; x <= cells; ++x) {
                if (x != 0u && x != cells && z != 0u && z != cells) {
                    continue;
                }
                const VertexKey vertex{static_cast<int64_t>(key.x) * cells + x,
                                       static_cast<int64_t>(key.z) * cells + z};
                const BorderValue& value = values.at(vertex);
                const float inv = 1.0f / static_cast<float>((std::max)(value.count, 1u));
                const std::size_t index = static_cast<std::size_t>(z) * stride + x;
                const float oldHeight = chunk.heights[index];
                const float oldCoverage = chunk.coverage[index];
                const float nextHeight = static_cast<float>(value.height) * inv;
                const float nextCoverage = std::clamp(static_cast<float>(value.coverage) * inv, 0.0f, 1.0f);
                if (std::fabs(nextHeight - oldHeight) > 1.0e-7f) {
                    chunk.heights[index] = nextHeight;
                    chunkHeightChanged = true;
                }
                if (std::fabs(nextCoverage - oldCoverage) > 1.0e-7f) {
                    chunk.coverage[index] = nextCoverage;
                    chunkCoverageChanged = true;
                    coverageChanged = true;
                    MarkCoverageVertexDirty(key, x, z);
                }
                std::array<float, TERRAIN::kTerrainChunkMaxLayers> oldWeights{};
                float total = 0.0f;
                for (uint32_t layer = 0u; layer < layerCount; ++layer) {
                    float& weight = chunk.layerWeights[static_cast<std::size_t>(layer) * vertexCount + index];
                    oldWeights[layer] = weight;
                    weight = (std::max)(static_cast<float>(value.weights[layer]) * inv, 0.0f);
                    total += weight;
                }
                if (total <= 1.0e-6f) {
                    chunk.layerWeights[index] = 1.0f;
                } else {
                    for (uint32_t layer = 0u; layer < layerCount; ++layer) {
                        chunk.layerWeights[static_cast<std::size_t>(layer) * vertexCount + index] /= total;
                    }
                }
                for (uint32_t layer = 0u; layer < layerCount; ++layer) {
                    float& weight = chunk.layerWeights[static_cast<std::size_t>(layer) * vertexCount + index];
                    if (std::fabs(weight - oldWeights[layer]) > 1.0e-7f) {
                        chunkLayersChanged = true;
                    } else {
                        // Do not publish sub-epsilon averaging noise as
                        // authored content or generate a false revision.
                        weight = oldWeights[layer];
                    }
                }
            }
        }
        if (chunkHeightChanged || chunkCoverageChanged || chunkLayersChanged) {
            changedChunks.insert(key);
        }
        if (chunkHeightChanged && outHeightChanged != nullptr) {
            *outHeightChanged = true;
        }
        if (chunkLayersChanged && outLayersChanged != nullptr) {
            *outLayersChanged = true;
        }
    }
    return coverageChanged;
}

void TerrainSurfaceComponent::ResetTerrain() {
    if (surface_.chunks.empty()) {
        return;
    }
    if (geometryEditBatchActive_) {
        for (const auto& [key, chunk] : surface_.chunks) {
            (void)chunk;
            CaptureChunkBeforeEdit(key);
        }
    }
    surface_.chunks.clear();
    chunkRevisions_.clear();
    RecordFullChunkChange();
    ++revision_;
    MarkGeometryChanged();
    ++coverageRevision_;
    InvalidateCoverageCache();
    NotifyRenderStateDirty();
}

void TerrainSurfaceComponent::BeginGeometryEditBatch() noexcept {
    if (geometryEditBatchActive_) {
        return;
    }
    geometryEditBatchActive_ = true;
    geometryEditBatchDirty_ = false;
    geometryEditDeltaReady_ = false;
    geometryEditBeforeChunks_.clear();
    geometryEditBeforePresetId_ = surface_.presetId;
    geometryEditBeforeLayerPresetIds_ = surface_.layerPresetIds;
}

bool TerrainSurfaceComponent::EndGeometryEditBatch() noexcept {
    if (!geometryEditBatchActive_) {
        return false;
    }
    geometryEditBatchActive_ = false;
    const bool changed = geometryEditBatchDirty_;
    geometryEditBatchDirty_ = false;
    geometryEditDeltaReady_ = true;
    if (changed) {
        ++geometryRevision_;
        ++collisionRevision_;
    }
    return changed;
}

bool TerrainSurfaceComponent::ConsumeGeometryEditDelta(nlohmann::json& outBeforeDelta, nlohmann::json& outAfterDelta) {
    outBeforeDelta = nlohmann::json::object();
    outAfterDelta = nlohmann::json::object();
    if (geometryEditBatchActive_ || !geometryEditDeltaReady_) {
        return false;
    }

    nlohmann::json beforeChunks = nlohmann::json::array();
    nlohmann::json afterChunks = nlohmann::json::array();
    for (const auto& [key, before] : geometryEditBeforeChunks_) {
        std::optional<TERRAIN::TerrainChunk> after{};
        if (const auto found = surface_.chunks.find(key); found != surface_.chunks.end()) {
            after = found->second;
        }
        const bool equal = before.has_value() == after.has_value() && (!before.has_value() || *before == *after);
        if (equal) {
            continue;
        }
        beforeChunks.push_back(SerializeTerrainChunkDeltaEntry(key, before));
        afterChunks.push_back(SerializeTerrainChunkDeltaEntry(key, after));
    }

    const bool presetChanged = geometryEditBeforePresetId_ != surface_.presetId;
    if (presetChanged) {
        outBeforeDelta["presetId"] = geometryEditBeforePresetId_;
        outAfterDelta["presetId"] = surface_.presetId;
    }
    const bool layerMappingChanged = geometryEditBeforeLayerPresetIds_ != surface_.layerPresetIds;
    if (layerMappingChanged) {
        outBeforeDelta["layerPresetIds"] = geometryEditBeforeLayerPresetIds_;
        outAfterDelta["layerPresetIds"] = surface_.layerPresetIds;
    }
    if (!beforeChunks.empty()) {
        outBeforeDelta["chunks"] = std::move(beforeChunks);
        outAfterDelta["chunks"] = std::move(afterChunks);
    }

    geometryEditBeforeChunks_.clear();
    geometryEditBeforePresetId_.clear();
    geometryEditBeforeLayerPresetIds_.clear();
    geometryEditDeltaReady_ = false;
    return presetChanged || layerMappingChanged || outBeforeDelta.contains("chunks");
}

void TerrainSurfaceComponent::CaptureChunkBeforeEdit(const TERRAIN::TerrainChunkKey& key) {
    if (!geometryEditBatchActive_ || geometryEditBeforeChunks_.contains(key)) {
        return;
    }
    if (const auto found = surface_.chunks.find(key); found != surface_.chunks.end()) {
        geometryEditBeforeChunks_.emplace(key, found->second);
    } else {
        geometryEditBeforeChunks_.emplace(key, std::nullopt);
    }
}

void TerrainSurfaceComponent::MarkChunkChanged(const TERRAIN::TerrainChunkKey& key) noexcept {
    chunkRevisions_[key] = nextChunkRevision_++;
    if (nextChunkRevision_ == 0u) {
        // 0 is reserved for a missing chunk. Wrap is practically
        // unreachable, but keep the contract explicit.
        nextChunkRevision_ = 1u;
    }
    RecordChunkChange(key);
}

void TerrainSurfaceComponent::RecordChunkChange(const TERRAIN::TerrainChunkKey& key) noexcept {
    chunkChangeJournal_.RecordChunk(key);
}

void TerrainSurfaceComponent::RecordFullChunkChange() noexcept { chunkChangeJournal_.RecordFullRebuild(); }

void TerrainSurfaceComponent::MarkGeometryChanged() noexcept {
    if (geometryEditBatchActive_) {
        geometryEditBatchDirty_ = true;
        return;
    }
    ++geometryRevision_;
    ++collisionRevision_;
}

} // namespace HIKARI
