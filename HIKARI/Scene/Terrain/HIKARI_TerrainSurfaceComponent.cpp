#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "Scene/Terrain/HIKARI_TerrainSurfaceSerialization.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Terrain/HIKARI_SurfaceLayerRegistry.h"

namespace HIKARI {

namespace {

bool TerrainMaterialEqual(const TERRAIN::GranularMaterialParams& lhs,
                          const TERRAIN::GranularMaterialParams& rhs) noexcept {
    return lhs.lightTint == rhs.lightTint && lhs.darkTint == rhs.darkTint &&
           lhs.sparkleStrength == rhs.sparkleStrength && lhs.rippleAmount == rhs.rippleAmount &&
           lhs.pitDarkening == rhs.pitDarkening && lhs.toonBandCount == rhs.toonBandCount &&
           lhs.toonBandHardness == rhs.toonBandHardness;
}

bool TerrainSettingsEqual(const TERRAIN::TerrainSurface& lhs, const TERRAIN::TerrainSurface& rhs) noexcept {
    return lhs.presetId == rhs.presetId && TerrainMaterialEqual(lhs.material, rhs.material) &&
           lhs.stylization == rhs.stylization && lhs.deform == rhs.deform && lhs.shoreline == rhs.shoreline &&
           lhs.heightScale == rhs.heightScale && lhs.layerPresetIds == rhs.layerPresetIds && lhs.brush == rhs.brush &&
           lhs.grass == rhs.grass && lhs.chunkCells == rhs.chunkCells && lhs.chunkCellSize == rhs.chunkCellSize;
}
} // namespace

const TERRAIN::TerrainSurface& TerrainSurfaceComponent::GetSurface() const noexcept { return surface_; }

void TerrainSurfaceComponent::SetSurface(const TERRAIN::TerrainSurface& surface) { SetSurfaceOwned(surface); }

void TerrainSurfaceComponent::SetSurfaceOwned(TERRAIN::TerrainSurface next) {
    next.chunkCells = (std::clamp)(next.chunkCells, 1u, TERRAIN::kTerrainChunkMaxCells);
    next.chunkCellSize = TERRAIN::SanitizeTerrainChunkCellSize(next.chunkCellSize);
    next.stylization = TERRAIN::SanitizeTerrainStylizationSettings(next.stylization);
    next.grass = TERRAIN::SanitizeTerrainGrassSettings(next.grass);
    for (const auto& [key, chunk] : next.chunks) {
        (void)key;
        if (!TERRAIN::TerrainChunkValid(chunk) || chunk.cells != next.chunkCells) {
            // A malformed document update must not erase an already
            // usable runtime terrain. The caller can rebuild/report the
            // rejected document state without publishing partial data.
            return;
        }
    }

    bool chunkGeometryChanged = false;
    bool chunkCoverageChanged = false;
    bool chunkLayersChanged = false;
    bool chunkTopologyChanged = false;
    std::vector<TERRAIN::TerrainChunkKey> payloadChangedChunks{};
    payloadChangedChunks.reserve(next.chunks.size());
    auto previousChunk = surface_.chunks.begin();
    auto nextChunk = next.chunks.begin();
    while (previousChunk != surface_.chunks.end() || nextChunk != next.chunks.end()) {
        if (previousChunk == surface_.chunks.end() ||
            (nextChunk != next.chunks.end() && nextChunk->first < previousChunk->first)) {
            chunkGeometryChanged = true;
            chunkCoverageChanged = true;
            chunkLayersChanged = true;
            chunkTopologyChanged = true;
            payloadChangedChunks.push_back(nextChunk->first);
            ++nextChunk;
            continue;
        }
        if (nextChunk == next.chunks.end() || previousChunk->first < nextChunk->first) {
            chunkGeometryChanged = true;
            chunkCoverageChanged = true;
            chunkLayersChanged = true;
            chunkTopologyChanged = true;
            ++previousChunk;
            continue;
        }

        const TERRAIN::TerrainChunk& previousPayload = previousChunk->second;
        const TERRAIN::TerrainChunk& nextPayload = nextChunk->second;
        const bool cellsChanged = previousPayload.cells != nextPayload.cells;
        const bool geometryPayloadChanged = cellsChanged || previousPayload.heights != nextPayload.heights;
        const bool coveragePayloadChanged = cellsChanged || previousPayload.coverage != nextPayload.coverage;
        const bool layersPayloadChanged = cellsChanged || previousPayload.layerCount != nextPayload.layerCount ||
                                          previousPayload.layerWeights != nextPayload.layerWeights;
        chunkGeometryChanged = chunkGeometryChanged || geometryPayloadChanged;
        chunkCoverageChanged = chunkCoverageChanged || coveragePayloadChanged;
        chunkLayersChanged = chunkLayersChanged || layersPayloadChanged;
        if (geometryPayloadChanged || coveragePayloadChanged || layersPayloadChanged) {
            payloadChangedChunks.push_back(nextChunk->first);
        }
        ++previousChunk;
        ++nextChunk;
    }

    bool geometryChanged = surface_.chunkCells != next.chunkCells || surface_.chunkCellSize != next.chunkCellSize ||
                           surface_.heightScale != next.heightScale || chunkGeometryChanged;
    bool coverageChanged =
        surface_.chunkCells != next.chunkCells || surface_.chunkCellSize != next.chunkCellSize || chunkCoverageChanged;
    bool layersChanged = surface_.layerPresetIds != next.layerPresetIds || chunkLayersChanged;
    const bool settingsChanged = !TerrainSettingsEqual(surface_, next);
    if (!geometryChanged && !coverageChanged && !layersChanged && !settingsChanged) {
        return;
    }

    const bool fullChunkProjectionChanged = chunkTopologyChanged || surface_.chunkCells != next.chunkCells ||
                                            surface_.chunkCellSize != next.chunkCellSize || settingsChanged;
    const bool coverageCachePolicyChanged = chunkTopologyChanged || surface_.chunkCells != next.chunkCells ||
                                            surface_.chunkCellSize != next.chunkCellSize ||
                                            surface_.shoreline.edgeFalloff != next.shoreline.edgeFalloff;

    surface_ = std::move(next);
    if (fullChunkProjectionChanged) {
        RecordFullChunkChange();
    }
    for (auto it = chunkRevisions_.begin(); it != chunkRevisions_.end();) {
        if (surface_.chunks.contains(it->first)) {
            ++it;
        } else {
            it = chunkRevisions_.erase(it);
        }
    }
    std::set<TERRAIN::TerrainChunkKey> changedChunks{};
    if (!payloadChangedChunks.empty()) {
        changedChunks = std::set<TERRAIN::TerrainChunkKey>(payloadChangedChunks.begin(), payloadChangedChunks.end());
        bool seamHeightChanged = false;
        bool seamLayersChanged = false;
        coverageChanged =
            StitchChunkBorders(payloadChangedChunks, changedChunks, &seamHeightChanged, &seamLayersChanged) ||
            coverageChanged;
        geometryChanged = geometryChanged || seamHeightChanged;
        layersChanged = layersChanged || seamLayersChanged;
        for (const TERRAIN::TerrainChunkKey& key : changedChunks) {
            MarkChunkChanged(key);
        }
    }

    ++revision_;
    if (geometryChanged) {
        MarkGeometryChanged();
    }
    if (coverageChanged) {
        ++coverageRevision_;
        if (!geometryChanged) {
            ++collisionRevision_;
        }
        if (coverageCachePolicyChanged || changedChunks.empty()) {
            // Topology/grid/falloff changes invalidate the distance-field
            // domain. Lost history deliberately falls back owner-wide.
            InvalidateCoverageCache();
        } else {
            for (const TERRAIN::TerrainChunkKey key : changedChunks) {
                MarkCoverageChunkDirty(key);
            }
        }
    }
    NotifyRenderStateDirty();
}

const TERRAIN::TerrainStylizationSettings& TerrainSurfaceComponent::GetStylizationSettings() const noexcept {
    return surface_.stylization;
}

void TerrainSurfaceComponent::SetStylizationSettings(const TERRAIN::TerrainStylizationSettings& settings) noexcept {
    const TERRAIN::TerrainStylizationSettings sanitized = TERRAIN::SanitizeTerrainStylizationSettings(settings);
    const TERRAIN::TerrainStylizationSettings& current = surface_.stylization;
    if (current.textureScale == sanitized.textureScale && current.textureStrength == sanitized.textureStrength &&
        current.normalStrength == sanitized.normalStrength && current.saturation == sanitized.saturation &&
        current.shadowStrength == sanitized.shadowStrength && current.rockSaturation == sanitized.rockSaturation &&
        current.facetSizeMeters == sanitized.facetSizeMeters && current.facetStrength == sanitized.facetStrength) {
        return;
    }
    surface_.stylization = sanitized;
    RecordFullChunkChange();
    ++revision_;
    // 見た目だけの変更なので geometry / coverage revision は無効化せず、
    // 地形衝突 lattice も再構築しない。
    NotifyRenderStateDirty();
}

const TERRAIN::TerrainGrassSettings& TerrainSurfaceComponent::GetGrassSettings() const noexcept {
    return surface_.grass;
}

void TerrainSurfaceComponent::SetGrassSettings(const TERRAIN::TerrainGrassSettings& settings) noexcept {
    const TERRAIN::TerrainGrassSettings sanitized = TERRAIN::SanitizeTerrainGrassSettings(settings);
    const TERRAIN::TerrainGrassSettings& current = surface_.grass;
    if (current.enabled == sanitized.enabled && current.density == sanitized.density &&
        current.bladeHeight == sanitized.bladeHeight && current.bladeWidth == sanitized.bladeWidth &&
        current.heightVariation == sanitized.heightVariation &&
        current.layerWeightThreshold == sanitized.layerWeightThreshold &&
        current.maxSlopeDegrees == sanitized.maxSlopeDegrees && current.windStrength == sanitized.windStrength &&
        current.windSpeed == sanitized.windSpeed && current.spatialFrequency == sanitized.spatialFrequency &&
        current.seed == sanitized.seed && current.maxClumps == sanitized.maxClumps) {
        return;
    }
    surface_.grass = sanitized;
    ++revision_;
    // Grass placement is a deterministic render projection. It does not
    // modify terrain height, coverage, or collision geometry.
    NotifyRenderStateDirty();
}

uint64_t TerrainSurfaceComponent::GetRevision() const noexcept { return revision_; }

uint64_t TerrainSurfaceComponent::GetGeometryRevision() const noexcept { return geometryRevision_; }

uint64_t TerrainSurfaceComponent::GetCoverageRevision() const noexcept { return coverageRevision_; }

uint64_t TerrainSurfaceComponent::GetCollisionRevision() const noexcept { return collisionRevision_; }

uint64_t TerrainSurfaceComponent::GetChunkRevision(const TERRAIN::TerrainChunkKey& key) const noexcept {
    if (!surface_.chunks.contains(key)) {
        return 0u;
    }
    const auto found = chunkRevisions_.find(key);
    return found != chunkRevisions_.end() ? found->second : 0u;
}

uint64_t TerrainSurfaceComponent::GetChunkChangeRevision() const noexcept { return chunkChangeJournal_.GetRevision(); }

bool TerrainSurfaceComponent::TryGetChangedChunkKeys(uint64_t sinceRevision,
                                                     std::vector<TERRAIN::TerrainChunkKey>& outKeys) const {
    return chunkChangeJournal_.TryGetChangedChunkKeys(sinceRevision, outKeys);
}

uint32_t TerrainSurfaceComponent::GetLayerCount() const noexcept {
    return (std::max)(static_cast<uint32_t>(surface_.layerPresetIds.size()), 1u);
}

const TERRAIN::TerrainChunkMap& TerrainSurfaceComponent::GetChunks() const noexcept { return surface_.chunks; }

uint32_t TerrainSurfaceComponent::GetChunkCells() const noexcept {
    return (std::clamp)(surface_.chunkCells, 1u, TERRAIN::kTerrainChunkMaxCells);
}

float TerrainSurfaceComponent::GetChunkWorldSize() const noexcept { return TERRAIN::TerrainChunkWorldSize(surface_); }

TERRAIN::TerrainGrassPlacementSnapshot TerrainSurfaceComponent::BuildGrassPlacementSnapshot() const {
    return TERRAIN::BuildTerrainGrassPlacementSnapshot(surface_.grass, GetChunkCells(), surface_.chunkCellSize,
                                                       surface_.chunks, surface_.layerPresetIds, surface_.presetId);
}

void TerrainSurfaceComponent::ApplySolidPreset(std::string_view presetId) {
    TERRAIN::SurfaceLayerRegistry::Get().RegisterBuiltins();
    const TERRAIN::SurfacePreset* found = TERRAIN::SurfaceLayerRegistry::Get().Find(presetId);
    std::optional<TERRAIN::SurfacePreset> fallback;
    if (found == nullptr) {
        fallback = TERRAIN::ResolveSurfacePreset(presetId);
        if (fallback.has_value()) {
            found = &*fallback;
        }
    }
    if (found == nullptr) {
        return;
    }
    TERRAIN::TerrainSurface next = surface_;
    next.presetId = found->id.empty() ? std::string(presetId) : found->id;
    if (next.layerPresetIds.empty()) {
        next.layerPresetIds.resize(1u);
    }
    next.layerPresetIds[0] = next.presetId;
    next.material = found->material;
    next.deform = found->deform;
    next.shoreline = found->shoreline;
    SetSurface(next);
}

bool TerrainSurfaceComponent::FillLayer(std::string_view presetId) {
    TERRAIN::SurfaceLayerRegistry::Get().RegisterBuiltins();
    const TERRAIN::SurfacePreset* found = TERRAIN::SurfaceLayerRegistry::Get().Find(presetId);
    std::optional<TERRAIN::SurfacePreset> fallback;
    if (found == nullptr) {
        fallback = TERRAIN::ResolveSurfacePreset(presetId);
        if (fallback.has_value()) {
            found = &*fallback;
        }
    }
    if (found == nullptr) {
        return false;
    }
    const int layerIndex =
        TERRAIN::SurfaceLayerRegistry::Get().ResolveIndex(found->id.empty() ? presetId : std::string_view(found->id));
    if (layerIndex < 0 || static_cast<uint32_t>(layerIndex) >= TERRAIN::kTerrainChunkMaxLayers) {
        return false;
    }
    const std::string targetId = found->id.empty() ? std::string(presetId) : found->id;
    TERRAIN::TerrainSurface next = surface_;
    if (next.layerPresetIds.size() <= static_cast<std::size_t>(layerIndex)) {
        next.layerPresetIds.resize(static_cast<std::size_t>(layerIndex) + 1u);
    }
    const bool presetMetadataChanged =
        next.presetId != targetId || next.layerPresetIds[static_cast<std::size_t>(layerIndex)] != targetId;
    bool changed = presetMetadataChanged;
    next.presetId = targetId;
    next.layerPresetIds[static_cast<std::size_t>(layerIndex)] = targetId;
    for (auto& [key, chunk] : next.chunks) {
        CaptureChunkBeforeEdit(key);
        const bool chunkChanged = TERRAIN::FillTerrainChunkLayer(chunk, static_cast<uint32_t>(layerIndex));
        if (chunkChanged) {
            MarkChunkChanged(key);
        }
        changed = chunkChanged || changed;
    }
    if (!changed) {
        return false;
    }
    // Fill は material layer weights と preset metadata だけを変える。
    // geometry / coverage / collision の projection はそのまま共有し、
    // render 側だけを一度 dirty にする。SetSurface は地形形状の更新
    // 用で、ここから呼ぶと不要な collision rebuild を起こす。
    surface_ = std::move(next);
    if (presetMetadataChanged) {
        RecordFullChunkChange();
    }
    ++revision_;
    NotifyRenderStateDirty();
    return true;
}

TERRAIN::TerrainBrush TerrainSurfaceComponent::GetBrush() const noexcept { return surface_.brush; }

void TerrainSurfaceComponent::SetBrush(const TERRAIN::TerrainBrush& brush) noexcept {
    surface_.brush = TERRAIN::SanitizeTerrainBrush(brush);
}

void TerrainSurfaceComponent::SetBrushMode(TERRAIN::TerrainBrushMode mode) { surface_.brush.mode = mode; }

void TerrainSurfaceComponent::SetBrushRadius(float radius) { surface_.brush.radius = (std::max)(radius, 0.05f); }

void TerrainSurfaceComponent::SetDeformParams(const TERRAIN::DeformParams& deform) {
    const TERRAIN::DeformParams& current = surface_.deform;
    if (current.behavior == deform.behavior && current.maxDepth == deform.maxDepth &&
        current.stampRadius == deform.stampRadius && current.rimStrength == deform.rimStrength &&
        current.responseResolutionScale == deform.responseResolutionScale &&
        current.restDepthScale == deform.restDepthScale && current.minimumDepth == deform.minimumDepth &&
        current.settleStartRatio == deform.settleStartRatio && current.settleSeconds == deform.settleSeconds &&
        current.impactDepthScale == deform.impactDepthScale && current.rimWidthScale == deform.rimWidthScale &&
        current.refillPerSecond == deform.refillPerSecond && current.diffusionPerSecond == deform.diffusionPerSecond &&
        current.plasticity == deform.plasticity && current.plasticRefillPerSecond == deform.plasticRefillPerSecond &&
        current.recoveryDelaySeconds == deform.recoveryDelaySeconds) {
        return;
    }
    const bool collisionGeometryChanged =
        current.rimStrength != deform.rimStrength && surface_.shoreline.edgeDepth <= 1.0e-4f;
    surface_.deform = deform;
    RecordFullChunkChange();
    ++revision_;
    if (collisionGeometryChanged) {
        MarkGeometryChanged();
    }
    NotifyRenderStateDirty();
}

void TerrainSurfaceComponent::SetShorelineParams(const TERRAIN::ShorelineParams& shoreline) {
    const TERRAIN::ShorelineParams& current = surface_.shoreline;
    if (current.edgeFalloff == shoreline.edgeFalloff && current.edgeDepth == shoreline.edgeDepth &&
        current.wetness == shoreline.wetness) {
        return;
    }
    const bool coverageChanged = current.edgeFalloff != shoreline.edgeFalloff;
    const bool collisionGeometryChanged = coverageChanged || current.edgeDepth != shoreline.edgeDepth;
    surface_.shoreline = shoreline;
    RecordFullChunkChange();
    ++revision_;
    if (collisionGeometryChanged) {
        MarkGeometryChanged();
    }
    if (coverageChanged) {
        ++coverageRevision_;
        InvalidateCoverageCache();
    }
    NotifyRenderStateDirty();
}

bool TerrainSurfaceComponent::ApplyProceduralTerrain(const TERRAIN::TerrainProceduralSpec& rawSpec,
                                                     TERRAIN::TerrainProceduralPreview& outPreview) {
    const TERRAIN::TerrainProceduralSpec spec = TERRAIN::SanitizeTerrainProceduralSpec(rawSpec);
    outPreview = TERRAIN::PreviewTerrainProcedural(surface_, spec);
    if (!outPreview.valid) {
        return false;
    }

    const float chunkSize = GetChunkWorldSize();
    const uint32_t cells = GetChunkCells();
    if (!std::isfinite(chunkSize) || chunkSize <= 1.0e-4f || cells == 0u) {
        return false;
    }
    const float cellSize = chunkSize / static_cast<float>(cells);
    const auto keyMin =
        TERRAIN::TerrainChunkKeyFromWorld(spec.centerX - spec.extentX, spec.centerZ - spec.extentZ, chunkSize);
    const auto keyMax =
        TERRAIN::TerrainChunkKeyFromWorld(spec.centerX + spec.extentX, spec.centerZ + spec.extentZ, chunkSize);

    bool changed = false;
    bool coverageChanged = false;
    std::vector<TERRAIN::TerrainChunkKey> touchedChunks{};
    touchedChunks.reserve(outPreview.affectedChunks);
    std::set<TERRAIN::TerrainChunkKey> changedChunks{};
    const auto chunkWouldChange = [&](const TERRAIN::TerrainChunk& chunk, int32_t chunkX, int32_t chunkZ) noexcept {
        const uint32_t stride = chunk.cells + 1u;
        for (uint32_t vz = 0u; vz <= chunk.cells; ++vz) {
            for (uint32_t vx = 0u; vx <= chunk.cells; ++vx) {
                const float localX = static_cast<float>(chunkX) * chunkSize + static_cast<float>(vx) * cellSize;
                const float localZ = static_cast<float>(chunkZ) * chunkSize + static_cast<float>(vz) * cellSize;
                const float mask = TERRAIN::EvaluateTerrainProceduralMask(spec, localX, localZ);
                if (mask <= 1.0e-5f) {
                    continue;
                }
                const std::size_t index = static_cast<std::size_t>(vz) * stride + vx;
                const float oldHeight = chunk.heights[index];
                const float oldCoverage = chunk.coverage[index];
                const float generated = TERRAIN::EvaluateTerrainProceduralHeight(spec, localX, localZ);
                const float target = spec.baseHeight + generated;
                const float amount = std::clamp(spec.blendAmount * mask, 0.0f, 1.0f);
                float nextHeight = oldHeight;
                switch (spec.blendMode) {
                case TERRAIN::TerrainProceduralBlendMode::Add: nextHeight += generated * amount; break;
                case TERRAIN::TerrainProceduralBlendMode::Replace: nextHeight += (target - nextHeight) * amount; break;
                case TERRAIN::TerrainProceduralBlendMode::Max:
                    nextHeight += ((std::max)(nextHeight, target) - nextHeight) * amount;
                    break;
                }
                const float coverageTarget = (std::max)(oldCoverage, mask);
                const float nextCoverage = oldCoverage + (coverageTarget - oldCoverage) * spec.blendAmount;
                if (std::fabs(nextHeight - oldHeight) > 1.0e-7f || std::fabs(nextCoverage - oldCoverage) > 1.0e-7f) {
                    return true;
                }
            }
        }
        return false;
    };
    for (int32_t z = keyMin.z; z <= keyMax.z; ++z) {
        for (int32_t x = keyMin.x; x <= keyMax.x; ++x) {
            const TERRAIN::TerrainChunkKey key{x, z};
            const auto found = surface_.chunks.find(key);
            TERRAIN::TerrainChunk candidate{};
            const TERRAIN::TerrainChunk* sourceChunk = nullptr;
            if (found == surface_.chunks.end()) {
                candidate = TERRAIN::MakeTerrainChunk(cells, GetLayerCount());
                std::fill(candidate.coverage.begin(), candidate.coverage.end(), 0.0f);
                (void)SeedChunkFromNeighborEdges(key, candidate);
                sourceChunk = &candidate;
            } else if (TERRAIN::TerrainChunkValid(found->second)) {
                sourceChunk = &found->second;
            }
            // Admit a new sparse chunk only after evaluating the exact
            // initialized grid and confirming a reversible height or
            // coverage delta. Tiny positive blends must remain no-ops.
            if (sourceChunk == nullptr || !chunkWouldChange(*sourceChunk, x, z)) {
                continue;
            }

            TERRAIN::TerrainChunk& chunk = found == surface_.chunks.end() ? EnsureChunk(key) : found->second;
            if (!TERRAIN::TerrainChunkValid(chunk)) {
                continue;
            }
            CaptureChunkBeforeEdit(key);
            touchedChunks.push_back(key);
            const uint32_t stride = chunk.cells + 1u;
            bool chunkChanged = false;
            for (uint32_t vz = 0u; vz <= chunk.cells; ++vz) {
                for (uint32_t vx = 0u; vx <= chunk.cells; ++vx) {
                    const float localX = static_cast<float>(x) * chunkSize + static_cast<float>(vx) * cellSize;
                    const float localZ = static_cast<float>(z) * chunkSize + static_cast<float>(vz) * cellSize;
                    const float mask = TERRAIN::EvaluateTerrainProceduralMask(spec, localX, localZ);
                    if (mask <= 1.0e-5f) {
                        continue;
                    }
                    const std::size_t index = static_cast<std::size_t>(vz) * stride + vx;
                    float& height = chunk.heights[index];
                    const float oldHeight = height;
                    const float generated = TERRAIN::EvaluateTerrainProceduralHeight(spec, localX, localZ);
                    const float target = spec.baseHeight + generated;
                    const float amount = std::clamp(spec.blendAmount * mask, 0.0f, 1.0f);
                    switch (spec.blendMode) {
                    case TERRAIN::TerrainProceduralBlendMode::Add: height += generated * amount; break;
                    case TERRAIN::TerrainProceduralBlendMode::Replace: height += (target - height) * amount; break;
                    case TERRAIN::TerrainProceduralBlendMode::Max:
                        height += ((std::max)(height, target) - height) * amount;
                        break;
                    }
                    float& coverage = chunk.coverage[index];
                    const float oldCoverage = coverage;
                    // Procedural generation is non-destructive to
                    // authored terrain footprint: every mode only grows
                    // coverage. Island/Coast creates a radial shoreline
                    // on empty terrain, but deliberately does not erase
                    // existing land or hand-authored holes outside its
                    // positive mask. Blend Amount controls both height
                    // and footprint admission.
                    const float coverageTarget = (std::max)(coverage, mask);
                    coverage += (coverageTarget - coverage) * spec.blendAmount;
                    const bool heightWasChanged = std::fabs(height - oldHeight) > 1.0e-7f;
                    const bool vertexCoverageChanged = std::fabs(coverage - oldCoverage) > 1.0e-7f;
                    chunkChanged = chunkChanged || heightWasChanged || vertexCoverageChanged;
                    changed = changed || chunkChanged;
                    coverageChanged = coverageChanged || vertexCoverageChanged;
                    if (vertexCoverageChanged) {
                        MarkCoverageVertexDirty(key, vx, vz);
                    }
                }
            }
            if (chunkChanged) {
                changedChunks.insert(key);
            }
        }
    }
    if (!changed) {
        return false;
    }

    coverageChanged = StitchChunkBorders(touchedChunks, changedChunks) || coverageChanged;
    for (const TERRAIN::TerrainChunkKey& key : changedChunks) {
        MarkChunkChanged(key);
    }
    ++revision_;
    MarkGeometryChanged();
    if (coverageChanged) {
        ++coverageRevision_;
    }
    NotifyRenderStateDirty();
    return true;
}

void TerrainSurfaceComponent::SerializeSettings(nlohmann::json& out) const {
    TERRAIN::SerializeTerrainSurfaceSettings(surface_, out);
}

void TerrainSurfaceComponent::Serialize(nlohmann::json& out) const { TERRAIN::SerializeTerrainSurface(surface_, out); }

void TerrainSurfaceComponent::Deserialize(const nlohmann::json& in) {
    TERRAIN::TerrainSurface surface{};
    if (TERRAIN::DeserializeTerrainSurface(in, surface)) {
        SetSurfaceOwned(std::move(surface));
    }
}

} // namespace HIKARI
