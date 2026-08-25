#include "Scene/Sand/HIKARI_SandSurfaceQueryService.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_set>

#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Core/Math/HIKARI_NormalMatrix.h"
#include "Scene/Terrain/HIKARI_SurfaceLayerRegistry.h"

namespace HIKARI::SAND {

    std::size_t SandSurfaceSnapshotChunkKeyHash::operator()(
        const SandSurfaceSnapshotChunkKey& key) const noexcept {
        uint64_t hash = HASH::Fnv1aCombine(
            HASH::kFnv1a64OffsetBasis, key.object);
        hash = HASH::Fnv1aCombine(
            hash,
            static_cast<uint64_t>(
                static_cast<uint32_t>(key.chunkX)));
        hash = HASH::Fnv1aCombine(
            hash,
            static_cast<uint64_t>(
                static_cast<uint32_t>(key.chunkZ)));
        return static_cast<std::size_t>(hash);
    }

    namespace {
        MATH::Vec3 TransformPoint(
            const MATH::Mat4& matrix,
            const MATH::Vec3& point) noexcept {
            const MATH::Vec4 transformed = matrix.TransformPoint({
                point.x, point.y, point.z, 1.0f });
            if (std::fabs(transformed.w) > 1.0e-6f) {
                const float invW = 1.0f / transformed.w;
                return {
                    transformed.x * invW,
                    transformed.y * invW,
                    transformed.z * invW };
            }
            return { transformed.x, transformed.y, transformed.z };
        }

        MATH::Vec3 TransformNormal(
            const MATH::Mat4& matrix,
            const MATH::Vec3& normal) noexcept {
            const MATH::Vec4 transformed = matrix.TransformPoint({
                normal.x, normal.y, normal.z, 0.0f });
            const MATH::Vec3 result{
                transformed.x, transformed.y, transformed.z };
            return MATH::Dot(result, result) > 1.0e-12f
                ? MATH::Normalize(result)
                : MATH::Vec3{ 0.0f, 1.0f, 0.0f };
        }

        float SampleChunkGrid(
            const SandChunkRuntimeState& chunk,
            const std::vector<float>& values,
            float localX,
            float localZ) noexcept {
            const uint32_t cells = (std::max)(chunk.cells, 1u);
            const uint32_t stride = cells + 1u;
            if (values.size() !=
                static_cast<std::size_t>(stride) * stride) {
                return 0.0f;
            }
            const float cellSize =
                (std::max)(chunk.cellSize, 1.0e-4f);
            const float fx = std::clamp(
                localX / cellSize, 0.0f, static_cast<float>(cells));
            const float fz = std::clamp(
                localZ / cellSize, 0.0f, static_cast<float>(cells));
            const uint32_t x0 = static_cast<uint32_t>(fx);
            const uint32_t z0 = static_cast<uint32_t>(fz);
            const uint32_t x1 = (std::min)(x0 + 1u, cells);
            const uint32_t z1 = (std::min)(z0 + 1u, cells);
            const float tx = fx - static_cast<float>(x0);
            const float tz = fz - static_cast<float>(z0);
            const float h00 = values[static_cast<std::size_t>(z0) * stride + x0];
            const float h10 = values[static_cast<std::size_t>(z0) * stride + x1];
            const float h01 = values[static_cast<std::size_t>(z1) * stride + x0];
            const float h11 = values[static_cast<std::size_t>(z1) * stride + x1];
            return std::lerp(
                std::lerp(h00, h10, tx),
                std::lerp(h01, h11, tx),
                tz);
        }

        float DeformableMaskAt(
            const SandChunkRuntimeState& chunk,
            std::size_t index,
            std::size_t vertexCount) noexcept {
            // Runtime がまだ旧 snapshot を受け取っている間は、mask 無しを
            // 従来の全体 deformation として扱い、地形を突然凍結させない。
            if (chunk.deformableMask.size() != vertexCount) {
                return 1.0f;
            }
            const float value = chunk.deformableMask[index];
            return std::isfinite(value)
                ? std::clamp(value, 0.0f, 1.0f)
                : 0.0f;
        }

        float SampleChunkFinalGrid(
            const SandChunkRuntimeState& chunk,
            float localX,
            float localZ) noexcept {
            const uint32_t cells = (std::max)(chunk.cells, 1u);
            const uint32_t stride = cells + 1u;
            const std::size_t vertexCount =
                static_cast<std::size_t>(stride) * stride;
            const bool composed =
                chunk.baseHeights.size() == vertexCount &&
                chunk.elasticHeights.size() == vertexCount &&
                chunk.plasticHeights.size() == vertexCount;
            if (!composed && chunk.heights.size() != vertexCount) {
                return 0.0f;
            }
            const float cellSize =
                (std::max)(chunk.cellSize, 1.0e-4f);
            const float fx = std::clamp(
                localX / cellSize, 0.0f, static_cast<float>(cells));
            const float fz = std::clamp(
                localZ / cellSize, 0.0f, static_cast<float>(cells));
            const uint32_t x0 = static_cast<uint32_t>(fx);
            const uint32_t z0 = static_cast<uint32_t>(fz);
            const uint32_t x1 = (std::min)(x0 + 1u, cells);
            const uint32_t z1 = (std::min)(z0 + 1u, cells);
            const float tx = fx - static_cast<float>(x0);
            const float tz = fz - static_cast<float>(z0);
            const auto valueAt = [&](uint32_t x, uint32_t z) noexcept {
                const std::size_t index =
                    static_cast<std::size_t>(z) * stride + x;
                if (!composed) {
                    return chunk.heights[index];
                }
                return EvaluateSandChunkFinalHeight(
                    chunk.baseHeights[index],
                    chunk.elasticHeights[index] +
                        chunk.plasticHeights[index],
                    DeformableMaskAt(chunk, index, vertexCount),
                    chunk.settings.deformationEnabled);
            };
            const float h00 = valueAt(x0, z0);
            const float h10 = valueAt(x1, z0);
            const float h01 = valueAt(x0, z1);
            const float h11 = valueAt(x1, z1);
            return std::lerp(
                std::lerp(h00, h10, tx),
                std::lerp(h01, h11, tx),
                tz);
        }

        constexpr std::array<std::string_view, 5u>
            kLegacyTerrainLayerPresetIds{
                "sand", "snow", "dirt", "grass", "rock" };

        std::string_view ResolveSandLayerPresetId(
            const TERRAIN::TerrainSurface& surface,
            uint32_t layer) noexcept {
            if (layer < surface.layerPresetIds.size() &&
                !surface.layerPresetIds[layer].empty()) {
                return surface.layerPresetIds[layer];
            }
            // 単一 solid preset の旧 scene は layerPresetIds を持たない。
            // 基底層だけは surface preset を優先し、grass / rock を砂として
            // 変形可能に戻さない。追加層は従来の index fallback を維持する。
            if (layer == 0u && !surface.presetId.empty()) {
                return surface.presetId;
            }
            return layer < kLegacyTerrainLayerPresetIds.size()
                ? kLegacyTerrainLayerPresetIds[layer]
                : kLegacyTerrainLayerPresetIds.front();
        }

        const TERRAIN::SurfacePreset* FindSandLayerPreset(
            const TERRAIN::TerrainSurface& surface,
            uint32_t layer,
            std::optional<TERRAIN::SurfacePreset>& fallback) noexcept {
            TERRAIN::SurfaceLayerRegistry& registry =
                TERRAIN::SurfaceLayerRegistry::Get();
            registry.RegisterBuiltins();
            const std::string_view id =
                ResolveSandLayerPresetId(surface, layer);
            if (const TERRAIN::SurfacePreset* preset = registry.Find(id);
                preset != nullptr) {
                return preset;
            }
            fallback = TERRAIN::ResolveSurfacePreset(id);
            return fallback.has_value() ? &*fallback : nullptr;
        }
    }

    bool IsSandLayerDeformable(
        const TERRAIN::TerrainSurface& surface,
        uint32_t layer) noexcept {
        std::optional<TERRAIN::SurfacePreset> fallback{};
        const TERRAIN::SurfacePreset* preset =
            FindSandLayerPreset(surface, layer, fallback);
        return preset != nullptr &&
            preset->deform.behavior != TERRAIN::DeformBehaviorId::None;
    }

    void ProjectSandChunkDeformableMask(
        const TERRAIN::TerrainSurface& surface,
        const TERRAIN::TerrainChunk& authored,
        uint32_t runtimeCells,
        std::vector<float>& outMask) {
        const uint32_t sourceCells = (std::max)(authored.cells, 1u);
        const uint32_t sourceStride = sourceCells + 1u;
        const uint32_t targetCells = (std::max)(runtimeCells, 1u);
        const uint32_t targetStride = targetCells + 1u;
        const std::size_t sourceCount =
            static_cast<std::size_t>(sourceStride) * sourceStride;
        const std::size_t targetCount =
            static_cast<std::size_t>(targetStride) * targetStride;
        outMask.assign(targetCount, 0.0f);

        const bool validWeights = authored.layerCount > 0u &&
            authored.layerCount <= TERRAIN::kTerrainChunkMaxLayers &&
            authored.layerWeights.size() == sourceCount *
                static_cast<std::size_t>(authored.layerCount);
        std::array<float, TERRAIN::kTerrainChunkMaxLayers>
            layerDeformable{};
        const uint32_t policyLayerCount = validWeights
            ? authored.layerCount : 1u;
        for (uint32_t layer = 0u;
            layer < policyLayerCount;
            ++layer) {
            layerDeformable[layer] = IsSandLayerDeformable(surface, layer)
                ? 1.0f : 0.0f;
        }
        std::vector<float> sourceMask(sourceCount, 0.0f);
        for (uint32_t z = 0u; z <= sourceCells; ++z) {
            for (uint32_t x = 0u; x <= sourceCells; ++x) {
                const std::size_t index =
                    static_cast<std::size_t>(z) * sourceStride + x;
                float mask = 0.0f;
                if (validWeights) {
                    for (uint32_t layer = 0u;
                        layer < authored.layerCount;
                        ++layer) {
                        const float weight = authored.layerWeights[
                            static_cast<std::size_t>(layer) * sourceCount +
                            index];
                        if (!std::isfinite(weight) || weight <= 0.0f) {
                            continue;
                        }
                        mask += std::clamp(weight, 0.0f, 1.0f) *
                            layerDeformable[layer];
                    }
                } else {
                    // 不完整または旧形式の chunk は、黙って非変形へ落とさず
                    // 従来どおり基底層を変形可能として扱う。
                    mask = layerDeformable[0u];
                }
                sourceMask[index] = std::clamp(mask, 0.0f, 1.0f);
            }
        }

        for (uint32_t z = 0u; z <= targetCells; ++z) {
            const float fz = static_cast<float>(z) *
                static_cast<float>(sourceCells) /
                static_cast<float>(targetCells);
            const uint32_t z0 = static_cast<uint32_t>(fz);
            const uint32_t z1 = (std::min)(z0 + 1u, sourceCells);
            const float tz = fz - static_cast<float>(z0);
            for (uint32_t x = 0u; x <= targetCells; ++x) {
                const float fx = static_cast<float>(x) *
                    static_cast<float>(sourceCells) /
                    static_cast<float>(targetCells);
                const uint32_t x0 = static_cast<uint32_t>(fx);
                const uint32_t x1 = (std::min)(x0 + 1u, sourceCells);
                const float tx = fx - static_cast<float>(x0);
                const auto source = [&](uint32_t sx, uint32_t sz) {
                    return sourceMask[
                        static_cast<std::size_t>(sz) * sourceStride + sx];
                };
                outMask[static_cast<std::size_t>(z) * targetStride + x] =
                    std::clamp(
                        std::lerp(
                            std::lerp(source(x0, z0), source(x1, z0), tx),
                            std::lerp(source(x0, z1), source(x1, z1), tx),
                            tz),
                        0.0f,
                        1.0f);
            }
        }
    }

    float EvaluateSandChunkFinalHeight(
        float baseHeight,
        float rawDeformation,
        float deformableMask,
        bool deformationEnabled) noexcept {
        const float mask = std::isfinite(deformableMask)
            ? std::clamp(deformableMask, 0.0f, 1.0f)
            : 0.0f;
        return baseHeight +
            (deformationEnabled ? rawDeformation * mask : 0.0f);
    }

    SandChunkRuntimeSnapshotPtr MakeSandChunkRuntimeSnapshot(
        const SandChunkRuntimeState& source) {
        auto snapshot = std::make_shared<SandChunkRuntimeState>();
        snapshot->key = source.key;
        snapshot->authoredCells = source.authoredCells;
        snapshot->cells = source.cells;
        snapshot->settings = source.settings;
        snapshot->chunkOriginLocal = source.chunkOriginLocal;
        snapshot->cellSize = source.cellSize;
        snapshot->heightScale = source.heightScale;
        snapshot->chunkWorldSize = source.chunkWorldSize;
        snapshot->deformHeights = source.deformHeights;
        snapshot->heights = source.heights;
        snapshot->coverage = source.coverage;
        snapshot->deformableMask = source.deformableMask;
        snapshot->responseGridActive = source.responseGridActive;
        snapshot->hasDeformation = source.hasDeformation;
        snapshot->stateRevision = source.stateRevision;
        return snapshot;
    }

    bool SandSurfaceQueryService::SampleChunkLocalHeight(
        const SandChunkRuntimeState& chunk,
        float localX,
        float localZ,
        float& outHeight) const noexcept {
        const std::size_t stride =
            static_cast<std::size_t>((std::max)(chunk.cells, 1u)) + 1u;
        const std::size_t vertexCount = stride * stride;
        const bool composed =
            chunk.baseHeights.size() == vertexCount &&
            chunk.elasticHeights.size() == vertexCount &&
            chunk.plasticHeights.size() == vertexCount;
        if (!composed && chunk.heights.size() != vertexCount) {
            outHeight = 0.0f;
            return false;
        }
        outHeight = SampleChunkFinalGrid(chunk, localX, localZ);
        return true;
    }

    bool SandSurfaceQueryService::SampleChunkLocalCoverage(
        const SandChunkRuntimeState& chunk,
        float localX,
        float localZ,
        float& outCoverage) const noexcept {
        const std::size_t stride =
            static_cast<std::size_t>((std::max)(chunk.cells, 1u)) + 1u;
        if (chunk.coverage.size() != stride * stride) {
            outCoverage = 0.0f;
            return false;
        }
        outCoverage = SampleChunkGrid(
            chunk, chunk.coverage, localX, localZ);
        return true;
    }

    bool SandSurfaceQueryService::SampleChunkLocalSurface(
        const SandChunkRuntimeState& chunk,
        float localX,
        float localZ,
        float& outHeight,
        float& outCoverage) const noexcept {
        outHeight = 0.0f;
        outCoverage = 0.0f;
        const std::size_t stride =
            static_cast<std::size_t>((std::max)(chunk.cells, 1u)) + 1u;
        const std::size_t sampleCount = stride * stride;
        const bool composed =
            chunk.baseHeights.size() == sampleCount &&
            chunk.elasticHeights.size() == sampleCount &&
            chunk.plasticHeights.size() == sampleCount;
        if ((!composed && chunk.heights.size() != sampleCount) ||
            chunk.coverage.size() != sampleCount) {
            return false;
        }
        outCoverage = SampleChunkGrid(
            chunk, chunk.coverage, localX, localZ);
        const float baseHeight = SampleChunkFinalGrid(
            chunk, localX, localZ);
        outHeight = EvaluateSandSurfaceEffectiveHeight(
            baseHeight,
            outCoverage,
            chunk.settings.edgeDepth,
            chunk.settings.rimStrength);
        return true;
    }

    bool SandSurfaceQueryService::SampleWorldHeight(
        const MATH::Vec3& worldPosition,
        float& outHeight,
        MATH::Vec3& outNormal) const noexcept {
        outHeight = 0.0f;
        outNormal = { 0.0f, 1.0f, 0.0f };
        const auto current = snapshotStore_.Current();
        if (!current || current->entries.empty()) {
            return false;
        }

        const auto sampleObjectLocal =
            [&](RuntimeObjectHandle object,
                float chunkWorldSize,
                float objectLocalX,
                float objectLocalZ,
                float& height,
                float& coverage) {
                bool sampled = false;
                float bestCoverage = -FLT_MAX;
                const auto toChunkCoordinate = [](
                    double coordinate,
                    float size,
                    int32_t& outCoordinate) noexcept {
                    if (!std::isfinite(coordinate) ||
                        !std::isfinite(size) || size <= 1.0e-4f) {
                        return false;
                    }
                    const double value = std::floor(
                        coordinate / static_cast<double>(size));
                    if (value < static_cast<double>(
                            (std::numeric_limits<int32_t>::min)()) ||
                        value > static_cast<double>(
                            (std::numeric_limits<int32_t>::max)())) {
                        return false;
                    }
                    outCoordinate = static_cast<int32_t>(value);
                    return true;
                };
                int32_t centerX = 0;
                int32_t centerZ = 0;
                if (!toChunkCoordinate(
                        objectLocalX,
                        chunkWorldSize,
                        centerX) ||
                    !toChunkCoordinate(
                        objectLocalZ,
                        chunkWorldSize,
                        centerZ)) {
                    return false;
                }
                // 1 点が接する grid chunk は最大 4 個。1-cell ring なら map を
                // 全走査せず負座標・境界上の従来 epsilon 挙動も保持できる。
                for (int32_t dz = -1; dz <= 1; ++dz) {
                    for (int32_t dx = -1; dx <= 1; ++dx) {
                        const int64_t candidateX =
                            static_cast<int64_t>(centerX) + dx;
                        const int64_t candidateZ =
                            static_cast<int64_t>(centerZ) + dz;
                        if (candidateX <
                                (std::numeric_limits<int32_t>::min)() ||
                            candidateX >
                                (std::numeric_limits<int32_t>::max)() ||
                            candidateZ <
                                (std::numeric_limits<int32_t>::min)() ||
                            candidateZ >
                                (std::numeric_limits<int32_t>::max)()) {
                            continue;
                        }
                        const auto indexed =
                            current->index.chunkEntryIndices.find({
                                object.ToValue(),
                                static_cast<int32_t>(candidateX),
                                static_cast<int32_t>(candidateZ),
                            });
                        if (indexed ==
                                current->index.chunkEntryIndices.end() ||
                            indexed->second >= current->entries.size()) {
                            continue;
                        }
                        const SandSurfaceSnapshotEntry& candidate =
                            current->entries[indexed->second];
                        if (candidate.object != object ||
                            !candidate.chunk) {
                            continue;
                        }
                        const SandChunkRuntimeState& candidateChunk =
                            *candidate.chunk;
                        const float candidateSize =
                            candidateChunk.chunkWorldSize;
                        if (candidateSize <= 1.0e-4f) {
                            continue;
                        }
                        const float x = objectLocalX -
                            candidateChunk.chunkOriginLocal.x;
                        const float z = objectLocalZ -
                            candidateChunk.chunkOriginLocal.y;
                        constexpr float epsilon = 1.0e-3f;
                        if (x < -epsilon ||
                            x > candidateSize + epsilon ||
                            z < -epsilon ||
                            z > candidateSize + epsilon) {
                            continue;
                        }
                        float candidateHeight = 0.0f;
                        float candidateCoverage = 0.0f;
                        if (!SampleChunkLocalSurface(
                                candidateChunk,
                                x,
                                z,
                                candidateHeight,
                                candidateCoverage)) {
                            continue;
                        }
                        // 共有境界では、より実体側の chunk を採用する。
                        if (!sampled ||
                            candidateCoverage > bestCoverage) {
                            sampled = true;
                            bestCoverage = candidateCoverage;
                            height = candidateHeight;
                            coverage = candidateCoverage;
                        }
                    }
                }
                return sampled;
            };

        float bestDistance = FLT_MAX;
        bool foundSurface = false;
        for (const std::size_t entryIndex :
                current->index.objectEntryIndices) {
            if (entryIndex >= current->entries.size()) {
                continue;
            }
            const SandSurfaceSnapshotEntry& entry =
                current->entries[entryIndex];
            if (!entry.chunk) {
                continue;
            }
            const SandChunkRuntimeState& chunk = *entry.chunk;
            const MATH::Vec3 queryLocal =
                TransformPoint(entry.worldToLocal, worldPosition);
            float localHeight = 0.0f;
            float coverage = 0.0f;
            if (!sampleObjectLocal(
                    entry.object,
                    chunk.chunkWorldSize,
                    queryLocal.x,
                    queryLocal.z,
                    localHeight,
                    coverage) ||
                coverage < kSandSurfaceVisibleCoverageThreshold) {
                continue;
            }

            const float cell = (std::max)(chunk.cellSize, 1.0e-4f);
            const auto sampleNeighbor = [&](float x, float z) {
                float neighborHeight = localHeight;
                float neighborCoverage = coverage;
                (void)sampleObjectLocal(
                    entry.object,
                    chunk.chunkWorldSize,
                    x,
                    z,
                    neighborHeight,
                    neighborCoverage);
                return neighborHeight;
            };
            const float hL = sampleNeighbor(
                queryLocal.x - cell, queryLocal.z);
            const float hR = sampleNeighbor(
                queryLocal.x + cell, queryLocal.z);
            const float hD = sampleNeighbor(
                queryLocal.x, queryLocal.z - cell);
            const float hU = sampleNeighbor(
                queryLocal.x, queryLocal.z + cell);
            MATH::Vec3 normal{
                (hL - hR) / (2.0f * cell),
                1.0f,
                (hD - hU) / (2.0f * cell)
            };
            const float lenSq = MATH::Dot(normal, normal);
            const MATH::Vec3 localNormal = lenSq > 1.0e-12f
                ? MATH::Normalize(normal)
                : MATH::Vec3{ 0.0f, 1.0f, 0.0f };
            const MATH::Vec3 surfaceWorld = TransformPoint(
                entry.localToWorld,
                {
                    queryLocal.x,
                    localHeight,
                    queryLocal.z
                });
            const float distance =
                std::fabs(worldPosition.y - surfaceWorld.y);
            if (distance < bestDistance) {
                bestDistance = distance;
                outHeight = surfaceWorld.y;
                outNormal = TransformNormal(
                    entry.normalMatrix, localNormal);
                foundSurface = true;
            }
        }
        return foundSurface;
    }

    bool SandSurfaceQueryService::TryGetChunkState(
        RuntimeObjectHandle object,
        TERRAIN::TerrainChunkKey key,
        SandChunkRuntimeSnapshotPtr& outChunk) const noexcept {
        outChunk.reset();
        const auto current = snapshotStore_.Current();
        if (!current) {
            return false;
        }
        const auto indexed = current->index.chunkEntryIndices.find({
            object.ToValue(), key.x, key.z });
        if (indexed == current->index.chunkEntryIndices.end() ||
            indexed->second >= current->entries.size()) {
            return false;
        }
        const SandSurfaceSnapshotEntry& entry =
            current->entries[indexed->second];
        if (entry.object == object && entry.chunk &&
            entry.chunk->key == key) {
            outChunk = entry.chunk;
            return true;
        }
        return false;
    }

    uint64_t SandSurfaceQueryService::GetSnapshotRevision() const noexcept {
        return snapshotStore_.Revision();
    }

    std::size_t SandSurfaceQueryService::GetSurfaceCount() const noexcept {
        return snapshotStore_.Count();
    }

    void SandSurfaceQueryService::PublishSnapshot(
        std::vector<SandSurfaceSnapshotEntry> entries) {
        SandSurfaceSnapshotIndex index{};
        index.objectEntryIndices.reserve(entries.size());
        index.chunkEntryIndices.reserve(entries.size());
        std::unordered_set<uint64_t> indexedObjects{};
        indexedObjects.reserve(entries.size());
        for (std::size_t entryIndex = 0u;
            entryIndex < entries.size();
            ++entryIndex) {
            const SandSurfaceSnapshotEntry& entry = entries[entryIndex];
            if (!entry.chunk) {
                continue;
            }
            const uint64_t objectKey = entry.object.ToValue();
            if (indexedObjects.insert(objectKey).second) {
                index.objectEntryIndices.push_back(entryIndex);
            }
            index.chunkEntryIndices.insert_or_assign(
                SandSurfaceSnapshotChunkKey{
                    objectKey,
                    entry.chunk->key.x,
                    entry.chunk->key.z,
                },
                entryIndex);
        }
        snapshotStore_.Publish(
            std::move(entries),
            std::move(index));
    }

    void SandSurfaceQueryService::Clear() {
        snapshotStore_.Clear();
    }

} // namespace HIKARI::SAND
