#include "Scene/Sand/HIKARI_SandSurfaceQueryService.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace HIKARI::SAND {

    namespace {
        float SampleChunkGrid(
            const SandChunkRuntimeState& chunk,
            float localX,
            float localZ) noexcept {
            const uint32_t cells = (std::max)(chunk.cells, 1u);
            const uint32_t stride = cells + 1u;
            if (chunk.heights.size() !=
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
            const std::vector<float>& h = chunk.heights;
            const float h00 = h[static_cast<std::size_t>(z0) * stride + x0];
            const float h10 = h[static_cast<std::size_t>(z0) * stride + x1];
            const float h01 = h[static_cast<std::size_t>(z1) * stride + x0];
            const float h11 = h[static_cast<std::size_t>(z1) * stride + x1];
            return std::lerp(
                std::lerp(h00, h10, tx),
                std::lerp(h01, h11, tx),
                tz);
        }
    }

    bool SandSurfaceQueryService::SampleChunkLocalHeight(
        const SandChunkRuntimeState& chunk,
        float localX,
        float localZ,
        float& outHeight) const noexcept {
        outHeight = SampleChunkGrid(chunk, localX, localZ);
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

        for (const SandSurfaceSnapshotEntry& entry : current->entries) {
            if (!entry.chunk) {
                continue;
            }
            const SandChunkRuntimeState& chunk = *entry.chunk;
            const float size = chunk.chunkWorldSize;
            if (size <= 1.0e-4f) {
                continue;
            }
            const float localX = worldPosition.x - chunk.chunkOriginWS.x;
            const float localZ = worldPosition.z - chunk.chunkOriginWS.y;
            if (localX < -1.0e-3f || localX > size + 1.0e-3f ||
                localZ < -1.0e-3f || localZ > size + 1.0e-3f) {
                continue;
            }
            outHeight = SampleChunkGrid(chunk, localX, localZ);

            const float cell = (std::max)(chunk.cellSize, 1.0e-4f);
            const float hL = SampleChunkGrid(chunk, localX - cell, localZ);
            const float hR = SampleChunkGrid(chunk, localX + cell, localZ);
            const float hD = SampleChunkGrid(chunk, localX, localZ - cell);
            const float hU = SampleChunkGrid(chunk, localX, localZ + cell);
            MATH::Vec3 normal{
                (hL - hR) / (2.0f * cell),
                1.0f,
                (hD - hU) / (2.0f * cell)
            };
            const float lenSq = MATH::Dot(normal, normal);
            outNormal = lenSq > 1.0e-12f
                ? MATH::Normalize(normal)
                : MATH::Vec3{ 0.0f, 1.0f, 0.0f };
            return true;
        }
        return false;
    }

    bool SandSurfaceQueryService::TryGetChunkHeights(
        RuntimeObjectHandle object,
        TERRAIN::TerrainChunkKey key,
        const std::vector<float>*& outHeights) const noexcept {
        outHeights = nullptr;
        const auto current = snapshotStore_.Current();
        if (!current) {
            return false;
        }
        for (const SandSurfaceSnapshotEntry& entry : current->entries) {
            if (entry.object == object &&
                entry.chunk &&
                entry.chunk->key == key) {
                outHeights = &entry.chunk->heights;
                return true;
            }
        }
        return false;
    }

    bool SandSurfaceQueryService::TryGetChunkState(
        RuntimeObjectHandle object,
        TERRAIN::TerrainChunkKey key,
        const SandChunkRuntimeState*& outChunk) const noexcept {
        outChunk = nullptr;
        const auto current = snapshotStore_.Current();
        if (!current) {
            return false;
        }
        for (const SandSurfaceSnapshotEntry& entry : current->entries) {
            if (entry.object == object &&
                entry.chunk &&
                entry.chunk->key == key) {
                outChunk = entry.chunk.get();
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
