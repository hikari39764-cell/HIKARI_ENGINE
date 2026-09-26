#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"
#include "Core/Math/HIKARI_GridBilinearSample.h"
#include "Core/Math/HIKARI_NormalMatrix.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceTypes.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>

namespace HIKARI {
namespace {
uint64_t PackTerrainVertexKey(int32_t x, int32_t z) noexcept {
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32u) | static_cast<uint32_t>(z);
}

template <class Sample>
bool VisitLocalSample(const TERRAIN::TerrainSurface& surface, float chunkSize, float x, float z,
                      Sample&& sample) noexcept {
    if (!std::isfinite(x) || !std::isfinite(z))
        return false;
    const double bx = std::floor(static_cast<double>(x) / chunkSize);
    const double bz = std::floor(static_cast<double>(z) / chunkSize);
    if (bx < INT32_MIN || bx > INT32_MAX || bz < INT32_MIN || bz > INT32_MAX)
        return false;
    // 共有辺では従来どおり右/上側を優先し、未生成なら隣接 chunk を読む。
    for (const int dz : {0, -1})
        for (const int dx : {0, -1}) {
            const int64_t cx = static_cast<int64_t>(bx) + dx;
            const int64_t cz = static_cast<int64_t>(bz) + dz;
            if (cx < INT32_MIN || cz < INT32_MIN)
                continue;
            const TERRAIN::TerrainChunkKey key{static_cast<int32_t>(cx), static_cast<int32_t>(cz)};
            const auto found = surface.chunks.find(key);
            if (found == surface.chunks.end() || !TERRAIN::TerrainChunkValid(found->second))
                continue;
            const auto& chunk = found->second;
            const float localX = x - static_cast<float>(key.x) * chunkSize;
            const float localZ = z - static_cast<float>(key.z) * chunkSize;
            constexpr float epsilon = 1.0e-4f;
            if (localX < -epsilon || localX > chunkSize + epsilon || localZ < -epsilon || localZ > chunkSize + epsilon)
                continue;
            const float cellSize = chunkSize / static_cast<float>(chunk.cells);
            sample(key, chunk, MATH::GridBilinearSample::Clamped(localX / cellSize, localZ / cellSize, chunk.cells));
            return true;
        }
    return false;
}
} // namespace

void TerrainSurfaceComponent::InvalidateCoverageCache() noexcept {
    coverageCache_.clear();
    coverageDistanceCache_.clear();
    coverageCacheRevision_ = 0u;
    coverageCacheInitialized_ = false;
    coverageCacheFullRebuild_ = true;
    coverageDirtyValid_ = false;
}

void TerrainSurfaceComponent::MarkCoverageVertexDirty(const TERRAIN::TerrainChunkKey& chunkKey, uint32_t localX,
                                                      uint32_t localZ) noexcept {
    const int64_t cells = static_cast<int64_t>(GetChunkCells());
    const int64_t globalX64 = static_cast<int64_t>(chunkKey.x) * cells + localX;
    const int64_t globalZ64 = static_cast<int64_t>(chunkKey.z) * cells + localZ;
    if (globalX64 < (std::numeric_limits<int32_t>::min)() || globalX64 > (std::numeric_limits<int32_t>::max)() ||
        globalZ64 < (std::numeric_limits<int32_t>::min)() || globalZ64 > (std::numeric_limits<int32_t>::max)()) {
        coverageCacheFullRebuild_ = true;
        coverageDirtyValid_ = false;
        return;
    }
    const int32_t globalX = static_cast<int32_t>(globalX64);
    const int32_t globalZ = static_cast<int32_t>(globalZ64);
    if (!coverageDirtyValid_) {
        coverageDirtyMinX_ = globalX;
        coverageDirtyMaxX_ = globalX;
        coverageDirtyMinZ_ = globalZ;
        coverageDirtyMaxZ_ = globalZ;
        coverageDirtyValid_ = true;
        return;
    }
    coverageDirtyMinX_ = (std::min)(coverageDirtyMinX_, globalX);
    coverageDirtyMaxX_ = (std::max)(coverageDirtyMaxX_, globalX);
    coverageDirtyMinZ_ = (std::min)(coverageDirtyMinZ_, globalZ);
    coverageDirtyMaxZ_ = (std::max)(coverageDirtyMaxZ_, globalZ);
}

void TerrainSurfaceComponent::MarkCoverageChunkDirty(const TERRAIN::TerrainChunkKey& chunkKey) noexcept {
    const uint32_t cells = GetChunkCells();
    MarkCoverageVertexDirty(chunkKey, 0u, 0u);
    MarkCoverageVertexDirty(chunkKey, cells, cells);
}

bool TerrainSurfaceComponent::TryReadCoverageVertex(int32_t globalX, int32_t globalZ,
                                                    float& outRawCoverage) const noexcept {
    outRawCoverage = 0.0f;
    const int64_t cells = static_cast<int64_t>(GetChunkCells());
    const auto floorDiv = [cells](int32_t value) noexcept {
        int64_t quotient = static_cast<int64_t>(value) / cells;
        const int64_t remainder = static_cast<int64_t>(value) % cells;
        if (remainder < 0) {
            --quotient;
        }
        return quotient;
    };
    const int64_t baseX = floorDiv(globalX);
    const int64_t baseZ = floorDiv(globalZ);
    bool foundAny = false;
    constexpr int64_t offsets[2] = {0, -1};
    for (const int64_t dz : offsets) {
        for (const int64_t dx : offsets) {
            const int64_t chunkX64 = baseX + dx;
            const int64_t chunkZ64 = baseZ + dz;
            if (chunkX64 < (std::numeric_limits<int32_t>::min)() || chunkX64 > (std::numeric_limits<int32_t>::max)() ||
                chunkZ64 < (std::numeric_limits<int32_t>::min)() || chunkZ64 > (std::numeric_limits<int32_t>::max)()) {
                continue;
            }
            const TERRAIN::TerrainChunkKey chunkKey{static_cast<int32_t>(chunkX64), static_cast<int32_t>(chunkZ64)};
            const auto found = surface_.chunks.find(chunkKey);
            if (found == surface_.chunks.end() || !TERRAIN::TerrainChunkValid(found->second)) {
                continue;
            }
            const TERRAIN::TerrainChunk& chunk = found->second;
            const int64_t localX = static_cast<int64_t>(globalX) - chunkX64 * cells;
            const int64_t localZ = static_cast<int64_t>(globalZ) - chunkZ64 * cells;
            if (localX < 0 || localZ < 0 || localX > static_cast<int64_t>(chunk.cells) ||
                localZ > static_cast<int64_t>(chunk.cells)) {
                continue;
            }
            const uint32_t x = static_cast<uint32_t>(localX);
            const uint32_t z = static_cast<uint32_t>(localZ);
            const uint32_t stride = chunk.cells + 1u;
            const float raw = std::clamp(chunk.coverage[static_cast<std::size_t>(z) * stride + x], 0.0f, 1.0f);
            outRawCoverage = (std::max)(outRawCoverage, raw);
            foundAny = true;
        }
    }
    return foundAny;
}

void TerrainSurfaceComponent::RebuildCoverageCache() const {
    struct CoverageNode {
        int32_t x = 0;
        int32_t z = 0;
        float raw = 0.0f;
        float distance = 0.0f;
    };
    struct QueueEntry {
        float distance = 0.0f;
        std::size_t index = 0u;
    };
    struct QueueGreater {
        bool operator()(const QueueEntry& lhs, const QueueEntry& rhs) const noexcept {
            return lhs.distance > rhs.distance;
        }
    };

    const float cellSize = TERRAIN::SanitizeTerrainChunkCellSize(surface_.chunkCellSize);
    const float width = (std::max)(surface_.shoreline.edgeFalloff, 0.0f);
    bool fullRebuild = coverageCacheFullRebuild_ || !coverageCacheInitialized_ || !coverageDirtyValid_;
    const int64_t radiusCells = width > cellSize * 0.5f ? static_cast<int64_t>(std::ceil(width / cellSize)) : 0;
    const auto expand = [](int32_t value, int64_t amount) {
        return static_cast<int32_t>(std::clamp(static_cast<int64_t>(value) + amount,
                                               static_cast<int64_t>((std::numeric_limits<int32_t>::min)()),
                                               static_cast<int64_t>((std::numeric_limits<int32_t>::max)())));
    };

    int32_t outputMinX = (std::numeric_limits<int32_t>::min)();
    int32_t outputMaxX = (std::numeric_limits<int32_t>::max)();
    int32_t outputMinZ = (std::numeric_limits<int32_t>::min)();
    int32_t outputMaxZ = (std::numeric_limits<int32_t>::max)();
    int32_t evalMinX = outputMinX;
    int32_t evalMaxX = outputMaxX;
    int32_t evalMinZ = outputMinZ;
    int32_t evalMaxZ = outputMaxZ;
    if (!fullRebuild) {
        outputMinX = expand(coverageDirtyMinX_, -radiusCells);
        outputMaxX = expand(coverageDirtyMaxX_, radiusCells);
        outputMinZ = expand(coverageDirtyMinZ_, -radiusCells);
        outputMaxZ = expand(coverageDirtyMaxZ_, radiusCells);
        // 1 頂点 halo は、影響範囲外の不変 distance field を境界条件
        // として再利用する。seed 消失による距離増加も局所で解き直せる。
        evalMinX = expand(outputMinX, -1);
        evalMaxX = expand(outputMaxX, 1);
        evalMinZ = expand(outputMinZ, -1);
        evalMaxZ = expand(outputMaxZ, 1);
    }
    const auto insideOutput = [&](int32_t x, int32_t z) {
        return x >= outputMinX && x <= outputMaxX && z >= outputMinZ && z <= outputMaxZ;
    };

    std::vector<CoverageNode> nodes{};
    std::unordered_map<uint64_t, std::size_t> nodeIndices{};
    const uint32_t cells = GetChunkCells();
    const auto appendChunk = [&](const TERRAIN::TerrainChunkKey& chunkKey, const TERRAIN::TerrainChunk& chunk) {
        if (!TERRAIN::TerrainChunkValid(chunk)) {
            return;
        }
        const uint32_t stride = chunk.cells + 1u;
        for (uint32_t z = 0u; z <= chunk.cells; ++z) {
            for (uint32_t x = 0u; x <= chunk.cells; ++x) {
                const int64_t globalX64 = static_cast<int64_t>(chunkKey.x) * cells + x;
                const int64_t globalZ64 = static_cast<int64_t>(chunkKey.z) * cells + z;
                if (globalX64 < evalMinX || globalX64 > evalMaxX || globalZ64 < evalMinZ || globalZ64 > evalMaxZ) {
                    continue;
                }
                const int32_t globalX = static_cast<int32_t>(globalX64);
                const int32_t globalZ = static_cast<int32_t>(globalZ64);
                const uint64_t packed = PackTerrainVertexKey(globalX, globalZ);
                const float raw = std::clamp(chunk.coverage[static_cast<std::size_t>(z) * stride + x], 0.0f, 1.0f);
                const auto [found, inserted] = nodeIndices.emplace(packed, nodes.size());
                if (inserted) {
                    nodes.push_back(CoverageNode{globalX, globalZ, raw, width});
                } else {
                    nodes[found->second].raw = (std::max)(nodes[found->second].raw, raw);
                }
            }
        }
    };

    if (fullRebuild) {
        coverageCache_.clear();
        coverageDistanceCache_.clear();
        for (const auto& [chunkKey, chunk] : surface_.chunks) {
            appendChunk(chunkKey, chunk);
        }
    } else {
        const auto floorDiv = [cells](int32_t value) {
            int64_t quotient = static_cast<int64_t>(value) / cells;
            const int64_t remainder = static_cast<int64_t>(value) % cells;
            if (remainder < 0) {
                --quotient;
            }
            return quotient;
        };
        // evalMin が共有 border 上の場合は左/下 chunk の duplicate
        // vertex も raw=max へ参加させる。
        const int64_t chunkMinX = floorDiv(evalMinX) - 1;
        const int64_t chunkMaxX = floorDiv(evalMaxX);
        const int64_t chunkMinZ = floorDiv(evalMinZ) - 1;
        const int64_t chunkMaxZ = floorDiv(evalMaxZ);
        const uint64_t chunkSpanX = static_cast<uint64_t>(chunkMaxX - chunkMinX + 1);
        const uint64_t chunkSpanZ = static_cast<uint64_t>(chunkMaxZ - chunkMinZ + 1);
        const uint64_t candidateChunkCount =
            chunkSpanX != 0u && chunkSpanZ <= (std::numeric_limits<uint64_t>::max)() / chunkSpanX
                ? chunkSpanX * chunkSpanZ
                : (std::numeric_limits<uint64_t>::max)();
        const uint64_t liveChunkCount = surface_.chunks.size();
        const uint64_t regionalBudget = liveChunkCount <= (std::numeric_limits<uint64_t>::max)() / 4u
                                            ? liveChunkCount * 4u
                                            : (std::numeric_limits<uint64_t>::max)();
        if (candidateChunkCount > regionalBudget) {
            // A very wide but legal shoreline falloff must never expand
            // an empty coordinate square. Rebuilding the sparse owner is
            // O(live chunks) and preserves the exact distance field.
            fullRebuild = true;
            coverageCache_.clear();
            coverageDistanceCache_.clear();
            for (const auto& [chunkKey, chunk] : surface_.chunks) {
                appendChunk(chunkKey, chunk);
            }
        } else {
            for (int64_t chunkZ = chunkMinZ; chunkZ <= chunkMaxZ; ++chunkZ) {
                for (int64_t chunkX = chunkMinX; chunkX <= chunkMaxX; ++chunkX) {
                    if (chunkX < (std::numeric_limits<int32_t>::min)() ||
                        chunkX > (std::numeric_limits<int32_t>::max)() ||
                        chunkZ < (std::numeric_limits<int32_t>::min)() ||
                        chunkZ > (std::numeric_limits<int32_t>::max)()) {
                        continue;
                    }
                    const TERRAIN::TerrainChunkKey key{static_cast<int32_t>(chunkX), static_cast<int32_t>(chunkZ)};
                    const auto found = surface_.chunks.find(key);
                    if (found != surface_.chunks.end()) {
                        appendChunk(found->first, found->second);
                    }
                }
            }
        }
    }

    constexpr std::array<std::pair<int32_t, int32_t>, 8u> offsets{{
        {-1, -1},
        {0, -1},
        {1, -1},
        {-1, 0},
        {1, 0},
        {-1, 1},
        {0, 1},
        {1, 1},
    }};
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, QueueGreater> queue{};
    constexpr float threshold = TERRAIN::kTerrainSurfaceVisibleCoverageThreshold;
    if (width > cellSize * 0.5f) {
        for (std::size_t index = 0u; index < nodes.size(); ++index) {
            CoverageNode& node = nodes[index];
            if (!fullRebuild && !insideOutput(node.x, node.z)) {
                const auto cached = coverageDistanceCache_.find(PackTerrainVertexKey(node.x, node.z));
                if (cached != coverageDistanceCache_.end()) {
                    node.distance = std::clamp(cached->second, 0.0f, width);
                }
            } else if (node.raw < threshold) {
                node.distance = 0.0f;
            } else {
                bool touchesOutside = false;
                for (const auto [dx, dz] : offsets) {
                    float neighborRaw = 0.0f;
                    if (!TryReadCoverageVertex(node.x + dx, node.z + dz, neighborRaw)) {
                        touchesOutside = true;
                        break;
                    }
                }
                if (touchesOutside) {
                    node.distance = (std::min)(cellSize, width);
                }
            }
            if (node.distance + 1.0e-6f < width) {
                queue.push({node.distance, index});
            }
        }

        while (!queue.empty()) {
            const QueueEntry entry = queue.top();
            queue.pop();
            const CoverageNode& current = nodes[entry.index];
            if (entry.distance > current.distance + 1.0e-6f) {
                continue;
            }
            for (const auto [dx, dz] : offsets) {
                const auto found = nodeIndices.find(PackTerrainVertexKey(current.x + dx, current.z + dz));
                if (found == nodeIndices.end()) {
                    continue;
                }
                CoverageNode& neighbor = nodes[found->second];
                const float step = (dx != 0 && dz != 0) ? cellSize * 1.41421356237f : cellSize;
                const float candidate = entry.distance + step;
                if (candidate + 1.0e-6f >= width || candidate + 1.0e-6f >= neighbor.distance) {
                    continue;
                }
                neighbor.distance = candidate;
                queue.push({candidate, found->second});
            }
        }
    }

    for (const CoverageNode& node : nodes) {
        if (!fullRebuild && !insideOutput(node.x, node.z)) {
            continue;
        }
        const uint64_t packed = PackTerrainVertexKey(node.x, node.z);
        const float distance = width > cellSize * 0.5f ? std::clamp(node.distance, 0.0f, width) : width;
        const float t = width > 1.0e-6f ? std::clamp(distance / width, 0.0f, 1.0f) : 1.0f;
        const float feather = t * t * (3.0f - 2.0f * t);
        coverageDistanceCache_[packed] = distance;
        coverageCache_[packed] = (std::min)(node.raw, feather);
    }

    coverageCacheRevision_ = coverageRevision_;
    coverageCacheInitialized_ = true;
    coverageCacheFullRebuild_ = false;
    coverageDirtyValid_ = false;
}

bool TerrainSurfaceComponent::SampleLocalRaw(float x, float z, float& outHeight, float& outCoverage) const noexcept {
    outHeight = outCoverage = 0.0f;
    return VisitLocalSample(
        surface_, GetChunkWorldSize(), x, z,
        [&](const auto&, const TERRAIN::TerrainChunk& chunk, const MATH::GridBilinearSample& sample) {
            const auto index = [&](uint32_t vx, uint32_t vz) {
                return static_cast<size_t>(vz) * (chunk.cells + 1u) + vx;
            };
            outHeight = sample.Sample([&](auto vx, auto vz) { return chunk.heights[index(vx, vz)]; });
            outCoverage = sample.Sample([&](auto vx, auto vz) { return chunk.coverage[index(vx, vz)]; });
        });
}

float TerrainSurfaceComponent::SampleLocalCoverage(const MATH::Vec3& localPos) const noexcept {
    if (coverageCacheRevision_ != coverageRevision_)
        RebuildCoverageCache();
    float coverage = 0.0f;
    VisitLocalSample(surface_, GetChunkWorldSize(), localPos.x, localPos.z,
                     [&](const TERRAIN::TerrainChunkKey& key, const TERRAIN::TerrainChunk& chunk,
                         const MATH::GridBilinearSample& sample) {
                         coverage = sample.Sample([&](uint32_t x, uint32_t z) {
                             const int32_t gx = key.x * static_cast<int32_t>(chunk.cells) + static_cast<int32_t>(x);
                             const int32_t gz = key.z * static_cast<int32_t>(chunk.cells) + static_cast<int32_t>(z);
                             const auto cached = coverageCache_.find(PackTerrainVertexKey(gx, gz));
                             if (cached != coverageCache_.end())
                                 return cached->second;
                             // revision 外の未登録頂点だけ raw coverage へフォールバックする。
                             return std::clamp(chunk.coverage[static_cast<size_t>(z) * (chunk.cells + 1u) + x], 0.0f,
                                               1.0f);
                         });
                     });
    return std::clamp(coverage, 0.0f, 1.0f);
}

bool TerrainSurfaceComponent::SampleLocalSurfaceHeight(const MATH::Vec3& localPos, float& outHeight) const noexcept {
    float rawCoverage = 0.0f;
    if (!SampleLocalRaw(localPos.x, localPos.z, outHeight, rawCoverage)) {
        outHeight = 0.0f;
        return false;
    }
    const float coverage = SampleLocalCoverage(localPos);
    if (coverage < TERRAIN::kTerrainSurfaceVisibleCoverageThreshold) {
        outHeight = 0.0f;
        return false;
    }
    outHeight = TERRAIN::EvaluateTerrainSurfaceEffectiveHeight(outHeight, coverage, surface_.shoreline.edgeDepth,
                                                               surface_.deform.rimStrength);
    return true;
}

bool TerrainSurfaceComponent::SampleLocalHeight(const MATH::Vec3& localPos, float& outHeight,
                                                MATH::Vec3& outNormal) const noexcept {
    outHeight = 0.0f;
    outNormal = {0.0f, 1.0f, 0.0f};
    const float chunkSize = GetChunkWorldSize();
    const float cellSize = chunkSize / static_cast<float>((std::max)(GetChunkCells(), 1u));
    if (!SampleLocalSurfaceHeight(localPos, outHeight)) {
        return false;
    }
    float hL = outHeight;
    float hR = outHeight;
    float hD = outHeight;
    float hU = outHeight;
    const auto sampleEdgeHeight = [&](float sx, float sz, float fallback) {
        float height = fallback;
        if (!SampleLocalSurfaceHeight({sx, localPos.y, sz}, height)) {
            return fallback;
        }
        return height;
    };
    hL = sampleEdgeHeight(localPos.x - cellSize, localPos.z, hL);
    hR = sampleEdgeHeight(localPos.x + cellSize, localPos.z, hR);
    hD = sampleEdgeHeight(localPos.x, localPos.z - cellSize, hD);
    hU = sampleEdgeHeight(localPos.x, localPos.z + cellSize, hU);
    MATH::Vec3 normal{(hL - hR) / (2.0f * cellSize), 1.0f, (hD - hU) / (2.0f * cellSize)};
    const float lenSq = MATH::Dot(normal, normal);
    outNormal = lenSq > 1.0e-12f ? MATH::Normalize(normal) : MATH::Vec3{0.0f, 1.0f, 0.0f};
    return true;
}

bool TerrainSurfaceComponent::SampleWorldHeight(const MATH::Vec3& worldPos, float& outHeight,
                                                MATH::Vec3& outNormal) const noexcept {
    const MATH::Vec3 localPos = WorldToLocal(worldPos);
    float localHeight = 0.0f;
    MATH::Vec3 localNormal{};
    if (!SampleLocalHeight(localPos, localHeight, localNormal)) {
        outHeight = 0.0f;
        outNormal = {0.0f, 1.0f, 0.0f};
        return false;
    }
    const MATH::Vec3 surfaceWorld = LocalToWorld({localPos.x, localHeight, localPos.z});
    outHeight = surfaceWorld.y;
    const GameObject* owner = GetOwner();
    if (owner == nullptr) {
        outNormal = localNormal;
        return true;
    }
    const MATH::Mat4 normalMatrix = MATH::BuildNormalMatrixFromWorld(owner->GetTransform().GetWorldMatrix());
    const MATH::Vec4 transformed = normalMatrix.TransformPoint({localNormal.x, localNormal.y, localNormal.z, 0.0f});
    const MATH::Vec3 worldNormal{transformed.x, transformed.y, transformed.z};
    outNormal =
        MATH::Dot(worldNormal, worldNormal) > 1.0e-12f ? MATH::Normalize(worldNormal) : MATH::Vec3{0.0f, 1.0f, 0.0f};
    return true;
}

bool TerrainSurfaceComponent::RaycastWorld(const MATH::Vec3& rayOrigin, const MATH::Vec3& rayDirection,
                                           float maxDistance, MATH::Vec3& outPosition, MATH::Vec3& outNormal,
                                           bool allowBasePlaneFallback) const noexcept {
    const float directionLengthSq = MATH::Dot(rayDirection, rayDirection);
    if (directionLengthSq <= 1.0e-12f || maxDistance <= 0.0f) {
        return false;
    }
    const MATH::Vec3 direction = MATH::Normalize(rayDirection);
    const MATH::Vec3 localOrigin = WorldToLocal(rayOrigin);
    const MATH::Vec3 localNext = WorldToLocal(rayOrigin + direction);
    const MATH::Vec3 localDirection = localNext - localOrigin;

    const auto evaluate = [&](float t, float& above, MATH::Vec3& localPoint, bool& hasSurface) {
        localPoint = localOrigin + localDirection * t;
        float height = 0.0f;
        hasSurface = SampleLocalSurfaceHeight(localPoint, height);
        if (!hasSurface && !allowBasePlaneFallback) {
            return false;
        }
        above = localPoint.y - height;
        return true;
    };

    float marchDistance = maxDistance;
    if (localDirection.y < -1.0e-6f) {
        float minimumHeight = 0.0f;
        for (const auto& [key, chunk] : surface_.chunks) {
            (void)key;
            if (chunk.heights.empty()) {
                continue;
            }
            minimumHeight = (std::min)(minimumHeight, *std::min_element(chunk.heights.begin(), chunk.heights.end()));
        }
        minimumHeight -= (std::max)(surface_.shoreline.edgeDepth, 0.0f);
        const float distanceToLowest = (minimumHeight - localOrigin.y) / localDirection.y;
        if (distanceToLowest >= 0.0f) {
            marchDistance = (std::min)(maxDistance, distanceToLowest + GetChunkWorldSize() * 0.02f);
        }
    }
    const float cellSize = TERRAIN::SanitizeTerrainChunkCellSize(surface_.chunkCellSize);
    const float horizontalDistance =
        std::sqrt(localDirection.x * localDirection.x + localDirection.z * localDirection.z) * marchDistance;
    const uint32_t marchSteps =
        std::clamp(static_cast<uint32_t>(std::ceil(horizontalDistance / (cellSize * 0.5f))), 1u, 192u);
    float previousT = 0.0f;
    float previousAbove = 0.0f;
    MATH::Vec3 previousPoint{};
    bool previousSurface = false;
    bool hasPrevious = evaluate(previousT, previousAbove, previousPoint, previousSurface);
    for (uint32_t step = 1u; step <= marchSteps; ++step) {
        const float t = marchDistance * static_cast<float>(step) / static_cast<float>(marchSteps);
        float above = 0.0f;
        MATH::Vec3 point{};
        bool hasSurface = false;
        const bool valid = evaluate(t, above, point, hasSurface);
        if (hasPrevious && valid && previousAbove >= 0.0f && above <= 0.0f) {
            float lo = previousT;
            float hi = t;
            MATH::Vec3 hitPoint = point;
            bool hitSurface = hasSurface;
            for (uint32_t iteration = 0u; iteration < 16u; ++iteration) {
                const float mid = (lo + hi) * 0.5f;
                float midAbove = 0.0f;
                MATH::Vec3 midPoint{};
                bool midSurface = false;
                (void)evaluate(mid, midAbove, midPoint, midSurface);
                if (midAbove > 0.0f) {
                    lo = mid;
                } else {
                    hi = mid;
                    hitPoint = midPoint;
                    hitSurface = midSurface;
                }
            }
            float height = 0.0f;
            MATH::Vec3 hitNormal{0.0f, 1.0f, 0.0f};
            if (!SampleLocalHeight(hitPoint, height, hitNormal)) {
                height = 0.0f;
                hitNormal = {0.0f, 1.0f, 0.0f};
                hitSurface = false;
            }
            hitPoint.y = height;
            outPosition = LocalToWorld(hitPoint);
            const GameObject* owner = GetOwner();
            if (owner == nullptr) {
                outNormal = hitNormal;
            } else {
                const MATH::Mat4 normalMatrix =
                    MATH::BuildNormalMatrixFromWorld(owner->GetTransform().GetWorldMatrix());
                const MATH::Vec4 transformed =
                    normalMatrix.TransformPoint({hitNormal.x, hitNormal.y, hitNormal.z, 0.0f});
                const MATH::Vec3 worldNormal{transformed.x, transformed.y, transformed.z};
                outNormal = MATH::Dot(worldNormal, worldNormal) > 1.0e-12f ? MATH::Normalize(worldNormal)
                                                                           : MATH::Vec3{0.0f, 1.0f, 0.0f};
            }
            (void)hitSurface;
            return true;
        }
        previousT = t;
        previousAbove = above;
        previousPoint = point;
        previousSurface = hasSurface;
        hasPrevious = valid;
    }
    return false;
}

MATH::Vec3 TerrainSurfaceComponent::WorldToLocal(const MATH::Vec3& worldPos) const noexcept {
    const GameObject* owner = GetOwner();
    if (owner == nullptr) {
        return worldPos;
    }
    const MATH::Mat4 inv = MATH::Inverse(owner->GetTransform().GetWorldMatrix());
    const MATH::Vec4 local = inv.TransformPoint({worldPos.x, worldPos.y, worldPos.z, 1.0f});
    if (std::fabs(local.w) > 1.0e-6f) {
        const float invW = 1.0f / local.w;
        return {local.x * invW, local.y * invW, local.z * invW};
    }
    return {local.x, local.y, local.z};
}

MATH::Vec3 TerrainSurfaceComponent::WorldNormalToLocal(const MATH::Vec3& worldNormal) const noexcept {
    const GameObject* owner = GetOwner();
    MATH::Vec3 localNormal = worldNormal;
    if (owner != nullptr) {
        const MATH::Mat4 world = owner->GetTransform().GetWorldMatrix();
        // world normal = inverse-transpose(world) * local normal,
        // therefore the inverse mapping is transpose(world).
        localNormal = {world.m[0][0] * worldNormal.x + world.m[0][1] * worldNormal.y + world.m[0][2] * worldNormal.z,
                       world.m[1][0] * worldNormal.x + world.m[1][1] * worldNormal.y + world.m[1][2] * worldNormal.z,
                       world.m[2][0] * worldNormal.x + world.m[2][1] * worldNormal.y + world.m[2][2] * worldNormal.z};
    }
    return MATH::Dot(localNormal, localNormal) > 1.0e-12f ? MATH::Normalize(localNormal) : MATH::Vec3{0.0f, 0.0f, 1.0f};
}

MATH::Vec3 TerrainSurfaceComponent::LocalToWorld(const MATH::Vec3& localPos) const noexcept {
    const GameObject* owner = GetOwner();
    if (owner == nullptr) {
        return localPos;
    }
    const MATH::Vec4 world =
        owner->GetTransform().GetWorldMatrix().TransformPoint({localPos.x, localPos.y, localPos.z, 1.0f});
    if (std::fabs(world.w) > 1.0e-6f) {
        const float invW = 1.0f / world.w;
        return {world.x * invW, world.y * invW, world.z * invW};
    }
    return {world.x, world.y, world.z};
}

} // namespace HIKARI
