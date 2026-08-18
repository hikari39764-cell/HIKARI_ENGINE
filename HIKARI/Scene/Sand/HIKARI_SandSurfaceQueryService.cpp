#include "Scene/Sand/HIKARI_SandSurfaceQueryService.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <unordered_set>

#include "Core/Math/HIKARI_NormalMatrix.h"

namespace HIKARI::SAND {

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
        if (chunk.heights.size() != stride * stride) {
            outHeight = 0.0f;
            return false;
        }
        outHeight = SampleChunkGrid(
            chunk, chunk.heights, localX, localZ);
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
        if (chunk.heights.size() != sampleCount ||
            chunk.coverage.size() != sampleCount) {
            return false;
        }
        outCoverage = SampleChunkGrid(
            chunk, chunk.coverage, localX, localZ);
        const float baseHeight = SampleChunkGrid(
            chunk, chunk.heights, localX, localZ);
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
                float objectLocalX,
                float objectLocalZ,
                float& height,
                float& coverage) {
                bool sampled = false;
                float bestCoverage = -FLT_MAX;
                for (const SandSurfaceSnapshotEntry& candidate :
                    current->entries) {
                    if (candidate.object != object || !candidate.chunk) {
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
                    if (x < -epsilon || x > candidateSize + epsilon ||
                        z < -epsilon || z > candidateSize + epsilon) {
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
                    if (!sampled || candidateCoverage > bestCoverage) {
                        sampled = true;
                        bestCoverage = candidateCoverage;
                        height = candidateHeight;
                        coverage = candidateCoverage;
                    }
                }
                return sampled;
            };

        float bestDistance = FLT_MAX;
        bool foundSurface = false;
        std::unordered_set<uint64_t> processedObjects{};
        for (const SandSurfaceSnapshotEntry& entry : current->entries) {
            if (!entry.chunk) {
                continue;
            }
            if (!processedObjects.insert(entry.object.ToValue()).second) {
                continue;
            }
            const SandChunkRuntimeState& chunk = *entry.chunk;
            const MATH::Vec3 queryLocal =
                TransformPoint(entry.worldToLocal, worldPosition);
            float localHeight = 0.0f;
            float coverage = 0.0f;
            if (!sampleObjectLocal(
                    entry.object,
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
        for (const SandSurfaceSnapshotEntry& entry : current->entries) {
            if (entry.object == object &&
                entry.chunk &&
                entry.chunk->key == key) {
                outChunk = entry.chunk;
                return true;
            }
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
        snapshotStore_.Publish(std::move(entries));
    }

    void SandSurfaceQueryService::Clear() {
        snapshotStore_.Clear();
    }

} // namespace HIKARI::SAND
