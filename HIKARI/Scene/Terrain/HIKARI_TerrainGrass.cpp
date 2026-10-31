#include "Scene/Terrain/HIKARI_TerrainGrass.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Scene/Terrain/HIKARI_TerrainSurface.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>

namespace HIKARI::TERRAIN {
namespace {
constexpr std::string_view kGrassHashId = "terrain-grass";
constexpr float kPi = 3.14159265358979323846f;

uint64_t HashTerrainGrass(uint64_t seed, const TerrainChunkKey& chunk, uint32_t cellX, uint32_t cellZ,
                          uint32_t channel) noexcept {
    // Keep the already-authored grass distribution stable while
    // keeping grass placement independent from removed object scatter.
    uint64_t hash = 1469598103934665603ull ^ seed;
    for (const unsigned char byte : kGrassHashId) {
        hash ^= static_cast<uint64_t>(byte);
        hash *= 1099511628211ull;
    }
    const auto mixInteger = [&hash](uint64_t value) noexcept {
        hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
        hash *= 1099511628211ull;
    };
    mixInteger(static_cast<uint32_t>(chunk.x));
    mixInteger(static_cast<uint32_t>(chunk.z));
    mixInteger(cellX);
    mixInteger(cellZ);
    mixInteger(channel);
    hash += 0x9e3779b97f4a7c15ull;
    hash = (hash ^ (hash >> 30u)) * 0xbf58476d1ce4e5b9ull;
    hash = (hash ^ (hash >> 27u)) * 0x94d049bb133111ebull;
    return hash ^ (hash >> 31u);
}

float TerrainGrassUnitFloat(uint64_t hash) noexcept {
    constexpr double kInvTwoTo53 = 1.0 / 9007199254740992.0;
    return static_cast<float>(static_cast<double>(hash >> 11u) * kInvTwoTo53);
}

float SampleBilinear(float v00, float v10, float v01, float v11, float tx, float tz) noexcept {
    return std::lerp(std::lerp(v00, v10, tx), std::lerp(v01, v11, tx), tz);
}

bool IsGrassPreset(std::string_view presetId) {
    if (presetId.empty()) {
        return false;
    }
    if (presetId == "grass") {
        return true;
    }
    const std::optional<SurfacePreset> preset = ResolveSurfacePreset(presetId);
    return preset.has_value() && preset->layerMaterial.kind == SurfaceLayerMaterialKind::Grass;
}

uint32_t FindGrassLayer(const std::vector<std::string>& layerPresetIds, std::string_view basePresetId) {
    const uint32_t explicitCount =
        static_cast<uint32_t>((std::min)(layerPresetIds.size(), static_cast<std::size_t>(kTerrainChunkMaxLayers)));
    for (uint32_t layer = 0u; layer < explicitCount; ++layer) {
        if (IsGrassPreset(ResolveTerrainLayerPresetId(layerPresetIds, basePresetId, layer))) {
            return layer;
        }
    }
    if (layerPresetIds.empty() && IsGrassPreset(basePresetId)) {
        return 0u;
    }
    return kTerrainChunkMaxLayers;
}

bool ReadFourVertices(const std::vector<float>& values, std::size_t i00, std::size_t i10, std::size_t i01,
                      std::size_t i11, float& out00, float& out10, float& out01, float& out11) noexcept {
    if (i00 >= values.size() || i10 >= values.size() || i01 >= values.size() || i11 >= values.size()) {
        return false;
    }
    out00 = values[i00];
    out10 = values[i10];
    out01 = values[i01];
    out11 = values[i11];
    return std::isfinite(out00) && std::isfinite(out10) && std::isfinite(out01) && std::isfinite(out11);
}
} // namespace

TerrainGrassSettings SanitizeTerrainGrassSettings(TerrainGrassSettings value) noexcept {
    return JSONFIELDS::Sanitize(std::move(value), kTerrainGrassFields);
}

bool IsTerrainGrassPlacementValid(const TerrainGrassPlacement& placement) noexcept {
    return std::isfinite(placement.localPosition.x) && std::isfinite(placement.localPosition.y) &&
           std::isfinite(placement.localPosition.z) && std::isfinite(placement.heightScale) &&
           placement.heightScale > 0.0f && std::isfinite(placement.widthScale) && placement.widthScale > 0.0f &&
           std::isfinite(placement.yawDegrees) && placement.variantIndex < kTerrainGrassVariantCount;
}

bool TerrainGrassPlacementKeyLess(const TerrainGrassPlacement& lhs, const TerrainGrassPlacement& rhs) noexcept {
    if (lhs.stableHash != rhs.stableHash) {
        return lhs.stableHash < rhs.stableHash;
    }
    if (lhs.chunk.x != rhs.chunk.x) {
        return lhs.chunk.x < rhs.chunk.x;
    }
    if (lhs.chunk.z != rhs.chunk.z) {
        return lhs.chunk.z < rhs.chunk.z;
    }
    if (lhs.cellX != rhs.cellX) {
        return lhs.cellX < rhs.cellX;
    }
    if (lhs.cellZ != rhs.cellZ) {
        return lhs.cellZ < rhs.cellZ;
    }
    return lhs.variantIndex < rhs.variantIndex;
}

TerrainGrassPlacementSnapshot BuildTerrainGrassPlacementSnapshot(const TerrainGrassSettings& source,
                                                                 uint32_t chunkCells, float chunkCellSize,
                                                                 const TerrainChunkMap& chunks,
                                                                 const std::vector<std::string>& layerPresetIds,
                                                                 std::string_view basePresetId) {
    const TerrainGrassSettings settings = SanitizeTerrainGrassSettings(source);
    TerrainGrassPlacementSnapshot snapshot{};
    snapshot.seed = settings.seed;
    snapshot.grassLayerIndex = FindGrassLayer(layerPresetIds, basePresetId);
    if (!settings.enabled || settings.maxClumps == 0u || snapshot.grassLayerIndex >= kTerrainChunkMaxLayers ||
        !TerrainChunkCellCountIsSafe(chunkCells)) {
        return snapshot;
    }

    snapshot.enabled = true;
    std::size_t reserveCount = 0u;
    const std::size_t reserveLimit = settings.maxClumps;
    for (const auto& [chunkKey, chunk] : chunks) {
        (void)chunkKey;
        if (reserveCount >= reserveLimit || !TerrainChunkCellCountIsSafe(chunk.cells)) {
            continue;
        }
        const std::size_t cellCount = static_cast<std::size_t>(chunk.cells) * chunk.cells;
        reserveCount += (std::min)(cellCount, reserveLimit - reserveCount);
    }
    snapshot.placements.reserve(reserveCount);
    const float cellSize = (std::max)(MATH::FiniteOr(chunkCellSize, 0.25f), 1.0e-4f);
    const float configuredChunkWorldSize = static_cast<float>(chunkCells) * cellSize;
    if (!std::isfinite(configuredChunkWorldSize)) {
        snapshot.enabled = false;
        return snapshot;
    }

    // Do not stop at the first map entries. The resident budget is a
    // global terrain candidate budget, so all candidates must compete by
    // their stable hash to avoid chunk-order bias and hard empty edges on
    // large terrains.
    for (const auto& [chunkKey, chunk] : chunks) {
        if (!TerrainChunkValid(chunk) || snapshot.grassLayerIndex >= chunk.layerCount) {
            continue;
        }

        const uint32_t cells = chunk.cells;
        std::size_t vertexCount = 0u;
        if (!TryGetTerrainChunkVertexCount(cells, vertexCount)) {
            continue;
        }
        const uint32_t stride = cells + 1u;
        const float localCellSize =
            chunk.cells == chunkCells ? cellSize : configuredChunkWorldSize / static_cast<float>(chunk.cells);
        if (!std::isfinite(localCellSize) || localCellSize <= 0.0f) {
            continue;
        }
        const float chunkOriginX = static_cast<float>(chunkKey.x) * configuredChunkWorldSize;
        const float chunkOriginZ = static_cast<float>(chunkKey.z) * configuredChunkWorldSize;
        if (!std::isfinite(chunkOriginX) || !std::isfinite(chunkOriginZ)) {
            continue;
        }
        const std::size_t layerOffset = static_cast<std::size_t>(snapshot.grassLayerIndex) * vertexCount;
        if (layerOffset > chunk.layerWeights.size() || vertexCount > chunk.layerWeights.size() - layerOffset) {
            continue;
        }

        for (uint32_t z = 0u; z < cells; ++z) {
            for (uint32_t x = 0u; x < cells; ++x) {
                const std::size_t i00 = static_cast<std::size_t>(z) * stride + x;
                const std::size_t i10 = i00 + 1u;
                const std::size_t i01 = i00 + stride;
                const std::size_t i11 = i01 + 1u;
                float h00 = 0.0f;
                float h10 = 0.0f;
                float h01 = 0.0f;
                float h11 = 0.0f;
                float c00 = 0.0f;
                float c10 = 0.0f;
                float c01 = 0.0f;
                float c11 = 0.0f;
                if (!ReadFourVertices(chunk.heights, i00, i10, i01, i11, h00, h10, h01, h11) ||
                    !ReadFourVertices(chunk.coverage, i00, i10, i01, i11, c00, c10, c01, c11)) {
                    continue;
                }
                const float w00 = chunk.layerWeights[layerOffset + i00];
                const float w10 = chunk.layerWeights[layerOffset + i10];
                const float w01 = chunk.layerWeights[layerOffset + i01];
                const float w11 = chunk.layerWeights[layerOffset + i11];
                if (!std::isfinite(w00) || !std::isfinite(w10) || !std::isfinite(w01) || !std::isfinite(w11)) {
                    continue;
                }

                // Keep the jitter away from the exact cell edge so that
                // adjacent chunks do not produce a visible seam of roots.
                const float tx =
                    0.12f + 0.76f * TerrainGrassUnitFloat(HashTerrainGrass(settings.seed, chunkKey, x, z, 1u));
                const float tz =
                    0.12f + 0.76f * TerrainGrassUnitFloat(HashTerrainGrass(settings.seed, chunkKey, x, z, 2u));
                const float coverage = std::clamp(SampleBilinear(c00, c10, c01, c11, tx, tz), 0.0f, 1.0f);
                const float grassWeight = std::clamp(SampleBilinear(w00, w10, w01, w11, tx, tz), 0.0f, 1.0f);
                if (coverage <= 1.0e-4f || grassWeight < settings.layerWeightThreshold) {
                    continue;
                }

                const float height = SampleBilinear(h00, h10, h01, h11, tx, tz);
                const float dhdx = ((h10 + h11) - (h00 + h01)) * 0.5f / localCellSize;
                const float dhdz = ((h01 + h11) - (h00 + h10)) * 0.5f / localCellSize;
                if (!std::isfinite(height) || !std::isfinite(dhdx) || !std::isfinite(dhdz)) {
                    continue;
                }
                const float slopeDegrees = std::atan(std::sqrt(dhdx * dhdx + dhdz * dhdz)) * 180.0f / kPi;
                if (!std::isfinite(slopeDegrees) || slopeDegrees > settings.maxSlopeDegrees) {
                    continue;
                }

                const uint64_t stableHash = HashTerrainGrass(settings.seed, chunkKey, x, z, 0u);
                const float acceptance = std::clamp(settings.density * grassWeight * coverage, 0.0f, 1.0f);
                if (TerrainGrassUnitFloat(stableHash) > acceptance) {
                    continue;
                }

                TerrainGrassPlacement placement{};
                placement.chunk = chunkKey;
                placement.cellX = x;
                placement.cellZ = z;
                placement.variantIndex = static_cast<uint32_t>(HashTerrainGrass(settings.seed, chunkKey, x, z, 5u) %
                                                               kTerrainGrassVariantCount);
                placement.localPosition = {
                    chunkOriginX + (static_cast<float>(x) + tx) * localCellSize,
                    height,
                    chunkOriginZ + (static_cast<float>(z) + tz) * localCellSize,
                };
                placement.heightScale = std::max(
                    0.01f,
                    1.0f + (2.0f * TerrainGrassUnitFloat(HashTerrainGrass(settings.seed, chunkKey, x, z, 3u)) - 1.0f) *
                               settings.heightVariation);
                placement.widthScale =
                    0.86f + 0.28f * TerrainGrassUnitFloat(HashTerrainGrass(settings.seed, chunkKey, x, z, 6u));
                placement.yawDegrees =
                    360.0f * TerrainGrassUnitFloat(HashTerrainGrass(settings.seed, chunkKey, x, z, 4u));
                placement.stableHash = stableHash;
                if (!IsTerrainGrassPlacementValid(placement)) {
                    continue;
                }

                // Keep the smallest stable keys in a fixed-size max heap
                // (the root is the current worst candidate). This makes
                // the cap deterministic and spatially fair across all
                // chunks while keeping memory bounded at O(maxClumps).
                if (snapshot.placements.size() < settings.maxClumps) {
                    snapshot.placements.push_back(std::move(placement));
                    std::push_heap(snapshot.placements.begin(), snapshot.placements.end(),
                                   TerrainGrassPlacementKeyLess);
                } else if (TerrainGrassPlacementKeyLess(placement, snapshot.placements.front())) {
                    std::pop_heap(snapshot.placements.begin(), snapshot.placements.end(), TerrainGrassPlacementKeyLess);
                    snapshot.placements.back() = std::move(placement);
                    std::push_heap(snapshot.placements.begin(), snapshot.placements.end(),
                                   TerrainGrassPlacementKeyLess);
                }
            }
        }
    }
    std::sort(snapshot.placements.begin(), snapshot.placements.end(), TerrainGrassPlacementKeyLess);
    return snapshot;
}

} // namespace HIKARI::TERRAIN
