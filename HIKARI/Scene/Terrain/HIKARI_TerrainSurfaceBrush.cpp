#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"
#include "Scene/Terrain/HIKARI_SurfaceLayerRegistry.h"
#include "Core/Math/HIKARI_GridBilinearSample.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace HIKARI {
namespace {
constexpr float kPi = 3.14159265358979323846f;
// ブラシのフォールオフ。hardness が中心の平らさ、falloff が
// 縁のなだらかさを決める（成熟ツールの一般的なブラシモデル）。
// hardness=1 でほぼ一定、falloff=1 で滑らかに 0 へ落ちる。
float BrushFalloff(float distance, float radius, float hardness, float falloff) noexcept {
    if (radius <= 1.0e-4f || distance >= radius) {
        return 0.0f;
    }
    const float normalized = distance / radius;
    const float coreRadius = std::clamp(hardness, 0.0f, 0.999f);
    if (normalized <= coreRadius) {
        return 1.0f;
    }
    const float edgeT = std::clamp((1.0f - normalized) / (1.0f - coreRadius), 0.0f, 1.0f);
    const float smooth = edgeT * edgeT * (3.0f - 2.0f * edgeT);
    return std::lerp(edgeT, smooth, std::clamp(falloff, 0.0f, 1.0f));
}

// Target-blend tools use a continuous response instead of clamping
// strength * dt. This preserves nearly identical low-strength feel,
// remains stable up to the production 8x limit, and is independent
// from 30/60/120 Hz editor frame cadence.
float BrushResponse(float strength, float deltaSeconds, float falloff) noexcept {
    const float rate = (std::max)(strength, 0.0f) * (std::max)(falloff, 0.0f);
    const float dt = std::clamp(deltaSeconds, 0.0f, 1.0f);
    return std::clamp(1.0f - std::exp(-rate * dt), 0.0f, 1.0f);
}

struct BrushVertexRange {
    uint32_t minX = 1u;
    uint32_t maxX = 0u;
    uint32_t minZ = 1u;
    uint32_t maxZ = 0u;

    bool IsValid() const noexcept { return minX <= maxX && minZ <= maxZ; }
};

BrushVertexRange ResolveBrushVertexRange(const MATH::Vec3& localPos, float radius, const MATH::Vec2& chunkOrigin,
                                         float cellSize, uint32_t cells) noexcept {
    const float safeCellSize = (std::max)(cellSize, 1.0e-4f);
    const auto clampVertex = [cells](int32_t value) {
        return static_cast<uint32_t>(std::clamp(value, 0, static_cast<int32_t>(cells)));
    };
    BrushVertexRange range{};
    range.minX = clampVertex(static_cast<int32_t>(std::floor((localPos.x - radius - chunkOrigin.x) / safeCellSize)));
    range.maxX = clampVertex(static_cast<int32_t>(std::ceil((localPos.x + radius - chunkOrigin.x) / safeCellSize)));
    range.minZ = clampVertex(static_cast<int32_t>(std::floor((localPos.z - radius - chunkOrigin.y) / safeCellSize)));
    range.maxZ = clampVertex(static_cast<int32_t>(std::ceil((localPos.z + radius - chunkOrigin.y) / safeCellSize)));
    return range;
}

struct BrushVertex {
    uint32_t x, z;
    float localX, localZ, offsetX, offsetZ, distance, falloff;
};

template <class Visit>
void VisitBrushVertices(const MATH::Vec3& localPos, const TERRAIN::TerrainBrush& brush, const MATH::Vec2& chunkOrigin,
                        float cellSize, uint32_t cells, Visit&& visit) {
    const auto range = ResolveBrushVertexRange(localPos, brush.radius, chunkOrigin, cellSize, cells);
    if (!range.IsValid())
        return;
    for (uint32_t z = range.minZ; z <= range.maxZ; ++z) {
        for (uint32_t x = range.minX; x <= range.maxX; ++x) {
            BrushVertex point{};
            point.x = x;
            point.z = z;
            point.localX = chunkOrigin.x + static_cast<float>(x) * cellSize;
            point.localZ = chunkOrigin.y + static_cast<float>(z) * cellSize;
            point.offsetX = point.localX - localPos.x;
            point.offsetZ = point.localZ - localPos.z;
            point.distance = std::sqrt(point.offsetX * point.offsetX + point.offsetZ * point.offsetZ);
            point.falloff = BrushFalloff(point.distance, brush.radius, brush.hardness, brush.falloff);
            if (point.falloff > 0.0f)
                visit(point);
        }
    }
}

} // namespace

template <typename Fn>
void TerrainSurfaceComponent::ForEachChunkInBrushLocal(const MATH::Vec3& localPos, float radius,
                                                       bool createMissingChunks, Fn&& fn, int32_t seedLayer) {
    const float chunkSize = GetChunkWorldSize();
    const float r = (std::max)(radius, 0.05f);
    const auto keyMin = TERRAIN::TerrainChunkKeyFromWorld(localPos.x - r, localPos.z - r, chunkSize);
    const auto keyMax = TERRAIN::TerrainChunkKeyFromWorld(localPos.x + r, localPos.z + r, chunkSize);
    for (int32_t kz = keyMin.z; kz <= keyMax.z; ++kz) {
        for (int32_t kx = keyMin.x; kx <= keyMax.x; ++kx) {
            const MATH::Vec2 chunkOrigin{static_cast<float>(kx) * chunkSize, static_cast<float>(kz) * chunkSize};
            const float nearestX = std::clamp(localPos.x, chunkOrigin.x, chunkOrigin.x + chunkSize);
            const float nearestZ = std::clamp(localPos.z, chunkOrigin.y, chunkOrigin.y + chunkSize);
            const float dx = nearestX - localPos.x;
            const float dz = nearestZ - localPos.z;
            if (dx * dx + dz * dz >= r * r) {
                continue;
            }
            const float cellSize = chunkSize / static_cast<float>(GetChunkCells());
            const float nearestVertexX = chunkOrigin.x + std::clamp(std::round((localPos.x - chunkOrigin.x) / cellSize),
                                                                    0.0f, static_cast<float>(GetChunkCells())) *
                                                             cellSize;
            const float nearestVertexZ = chunkOrigin.y + std::clamp(std::round((localPos.z - chunkOrigin.y) / cellSize),
                                                                    0.0f, static_cast<float>(GetChunkCells())) *
                                                             cellSize;
            const float vertexDx = nearestVertexX - localPos.x;
            const float vertexDz = nearestVertexZ - localPos.z;
            if (vertexDx * vertexDx + vertexDz * vertexDz >= r * r) {
                continue;
            }
            const TERRAIN::TerrainChunkKey key{kx, kz};
            auto found = surface_.chunks.find(key);
            if (found == surface_.chunks.end() && !createMissingChunks) {
                continue;
            }
            TERRAIN::TerrainChunk& chunk = found != surface_.chunks.end() ? found->second : EnsureChunk(key, seedLayer);
            fn(key, chunk, chunkOrigin);
        }
    }
}

bool TerrainSurfaceComponent::SculptAtWorld(const MATH::Vec3& worldPos, float deltaSeconds) {
    return SculptAtLocal(WorldToLocal(worldPos), deltaSeconds);
}

bool TerrainSurfaceComponent::SculptAtLocal(const MATH::Vec3& localPos, float deltaSeconds) {
    const TERRAIN::TerrainBrush brush = TERRAIN::SanitizeTerrainBrush(surface_.brush);
    // strength をフレームレート非依存にするため dt を畳み込む。
    const float dtScale = std::clamp(deltaSeconds, 0.0f, 0.1f);
    if (brush.strength <= 1.0e-6f || dtScale <= 0.0f) {
        return false;
    }
    const float chunkSize = GetChunkWorldSize();
    const uint32_t cells = GetChunkCells();
    const float cellSize = chunkSize / static_cast<float>(cells);
    const bool isSmooth = brush.mode == TERRAIN::TerrainBrushMode::Smooth;
    // Smooth は近傍平均へ寄せるため、編集前の高さを参照する必要がある。
    // 書き込みながら読むと方向バイアスが出るので、先にスナップショット
    // を取り、それを読みながら新しい値を書く（2 パス相当）。
    std::map<TERRAIN::TerrainChunkKey, std::vector<float>> smoothSource{};
    if (isSmooth) {
        const float r = (std::max)(surface_.brush.radius, 0.05f);
        const auto kMin = TERRAIN::TerrainChunkKeyFromWorld(localPos.x - r, localPos.z - r, chunkSize);
        const auto kMax = TERRAIN::TerrainChunkKeyFromWorld(localPos.x + r, localPos.z + r, chunkSize);
        for (int32_t kz = kMin.z; kz <= kMax.z; ++kz) {
            for (int32_t kx = kMin.x; kx <= kMax.x; ++kx) {
                const TERRAIN::TerrainChunkKey key{kx, kz};
                const auto it = surface_.chunks.find(key);
                if (it != surface_.chunks.end()) {
                    smoothSource.emplace(key, it->second.heights);
                }
            }
        }
    }
    // ワールド XZ の高さをスナップショットから読む（chunk 非依存）。
    const auto sampleSource = [&](float lx, float lz, float& outH) -> bool {
        if (!isSmooth) {
            return false;
        }
        const TERRAIN::TerrainChunkKey key = TERRAIN::TerrainChunkKeyFromWorld(lx, lz, chunkSize);
        const auto it = smoothSource.find(key);
        if (it == smoothSource.end()) {
            return false;
        }
        const std::vector<float>& src = it->second;
        const float localX = lx - static_cast<float>(key.x) * chunkSize;
        const float localZ = lz - static_cast<float>(key.z) * chunkSize;
        const auto sample = MATH::GridBilinearSample::Clamped(localX / cellSize, localZ / cellSize, cells);
        outH =
            sample.Sample([&](uint32_t x, uint32_t z) { return src[static_cast<std::size_t>(z) * (cells + 1u) + x]; });
        return true;
    };

    const bool isRidge = brush.mode == TERRAIN::TerrainBrushMode::Ridge;
    const float ridgeAngle = brush.ridgeDirectionDegrees * kPi / 180.0f;
    const float ridgeAxisX = std::cos(ridgeAngle);
    const float ridgeAxisZ = std::sin(ridgeAngle);
    const float ridgePerpendicularX = -ridgeAxisZ;
    const float ridgePerpendicularZ = ridgeAxisX;
    const float ridgeWidth = brush.radius * std::lerp(0.55f, 0.10f, brush.ridgeSharpness);
    const float shaftFloorRadius = (std::max)(0.0f, brush.radius - (std::min)(brush.shaftWallWidth, brush.radius));
    const float shaftWallWidth = (std::max)(brush.radius - shaftFloorRadius, cellSize);

    bool changed = false;
    bool coverageChanged = false;
    std::vector<TERRAIN::TerrainChunkKey> touchedChunks{};
    std::set<TERRAIN::TerrainChunkKey> changedChunks{};
    ForEachChunkInBrushLocal(
        localPos, brush.radius, true,
        [&](const TERRAIN::TerrainChunkKey& key, TERRAIN::TerrainChunk& chunk, const MATH::Vec2& chunkOrigin) {
            touchedChunks.push_back(key);
            CaptureChunkBeforeEdit(key);
            const uint32_t stride = chunk.cells + 1u;
            bool chunkChanged = false;
            VisitBrushVertices(localPos, brush, chunkOrigin, cellSize, chunk.cells, [&](const BrushVertex& point) {
                const auto [x, z, lx, lz, dx, dz, dist, falloff] = point;
                float sculptFalloff = falloff;
                if (isRidge) {
                    const float along = dx * ridgeAxisX + dz * ridgeAxisZ;
                    const float across = dx * ridgePerpendicularX + dz * ridgePerpendicularZ;
                    sculptFalloff = BrushFalloff(std::fabs(across), ridgeWidth, brush.hardness, brush.falloff) *
                                    BrushFalloff(std::fabs(along), brush.radius, brush.hardness, brush.falloff);
                }
                if (sculptFalloff <= 0.0f) {
                    return;
                }
                float& h = chunk.heights[static_cast<std::size_t>(z) * stride + x];
                float& coverage = chunk.coverage[static_cast<std::size_t>(z) * stride + x];
                const float oldHeight = h;
                const float oldCoverage = coverage;
                coverage = (std::max)(coverage, sculptFalloff);
                // strength は 1 ストローク当たりの伸びとして扱い、
                // heightScale を掛けて見た目の起伏スケールに合わせる。
                const float amount = brush.strength * dtScale * sculptFalloff * surface_.heightScale;
                switch (brush.mode) {
                case TERRAIN::TerrainBrushMode::Raise: h += amount; break;
                case TERRAIN::TerrainBrushMode::Lower: h -= amount; break;
                case TERRAIN::TerrainBrushMode::Mound:
                    // 中心を強く持ち上げる盛り土。
                    h += amount * 2.0f;
                    break;
                case TERRAIN::TerrainBrushMode::Pit:
                    // 中心を強く掘る穴。
                    h -= amount * 2.0f;
                    break;
                case TERRAIN::TerrainBrushMode::Flatten: {
                    const float target = brush.flattenHeight;
                    h += (target - h) * BrushResponse(brush.strength, dtScale, falloff);
                    break;
                }
                case TERRAIN::TerrainBrushMode::Smooth: {
                    // 4 近傍との平均へ寄せる本物の平滑。
                    float self = 0.0f;
                    float sum = 0.0f;
                    uint32_t n = 0u;
                    if (sampleSource(lx, lz, self)) {
                        sum += self;
                        ++n;
                        float nv = 0.0f;
                        if (sampleSource(lx - cellSize, lz, nv)) {
                            sum += nv;
                            ++n;
                        }
                        if (sampleSource(lx + cellSize, lz, nv)) {
                            sum += nv;
                            ++n;
                        }
                        if (sampleSource(lx, lz - cellSize, nv)) {
                            sum += nv;
                            ++n;
                        }
                        if (sampleSource(lx, lz + cellSize, nv)) {
                            sum += nv;
                            ++n;
                        }
                    }
                    if (n > 1u) {
                        const float avg = sum / static_cast<float>(n);
                        h += (avg - h) * BrushResponse(brush.strength, dtScale, falloff);
                    }
                    break;
                }
                case TERRAIN::TerrainBrushMode::Ridge: {
                    // The line-shaped falloff was resolved above so
                    // coverage and height share the same ridge mask.
                    h += amount;
                    break;
                }
                case TERRAIN::TerrainBrushMode::Terrace: {
                    // Terrace moves the current height toward the
                    // nearest authored step. Unlike Raise/Lower this
                    // is a target blend, so use a 60 Hz reference to
                    // make strength meaningful for a single stroke.
                    const float step = (std::max)(brush.terraceStepHeight, 0.05f);
                    const float stepped = std::round(h / step) * step;
                    const float blend = BrushResponse(brush.strength * 60.0f, dtScale, falloff);
                    h += (stepped - h) * blend;
                    break;
                }
                case TERRAIN::TerrainBrushMode::Shaft: {
                    float target = brush.flattenHeight;
                    if (dist > shaftFloorRadius) {
                        const float wallT = std::clamp((dist - shaftFloorRadius) / shaftWallWidth, 0.0f, 1.0f);
                        const float smoothWall = wallT * wallT * (3.0f - 2.0f * wallT);
                        target = std::lerp(brush.flattenHeight, oldHeight, smoothWall);
                    }
                    h += (target - h) * BrushResponse(brush.strength * 2.0f, dtScale, falloff);
                    break;
                }
                case TERRAIN::TerrainBrushMode::Noise: {
                    const float noise =
                        TERRAIN::EvaluateTerrainAuthoringNoise(lx, lz, brush.noiseSeed, brush.noiseScale, 4u);
                    h += amount * brush.noiseAmplitude * noise;
                    break;
                }
                }
                const bool vertexCoverageChanged = std::fabs(coverage - oldCoverage) > 1.0e-7f;
                const bool vertexChanged = std::fabs(h - oldHeight) > 1.0e-7f || vertexCoverageChanged;
                changed = changed || vertexChanged;
                chunkChanged = chunkChanged || vertexChanged;
                coverageChanged = coverageChanged || vertexCoverageChanged;
                if (vertexCoverageChanged) {
                    MarkCoverageVertexDirty(key, x, z);
                }
            });
            if (chunkChanged) {
                changedChunks.insert(key);
            }
        });
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

bool TerrainSurfaceComponent::ApplyRampAtLocal(const TERRAIN::TerrainRampSpec& rawSpec) {
    const TERRAIN::TerrainRampSpec spec = TERRAIN::SanitizeTerrainRampSpec(rawSpec);
    const float segmentX = spec.end.x - spec.start.x;
    const float segmentZ = spec.end.z - spec.start.z;
    const float segmentLengthSquared = segmentX * segmentX + segmentZ * segmentZ;
    if (segmentLengthSquared <= 1.0e-6f || spec.blendAmount <= 1.0e-6f) {
        return false;
    }
    const float segmentLength = std::sqrt(segmentLengthSquared);
    const float outerWidth = spec.halfWidth + spec.shoulderWidth;
    const MATH::Vec3 center{(spec.start.x + spec.end.x) * 0.5f, (spec.start.y + spec.end.y) * 0.5f,
                            (spec.start.z + spec.end.z) * 0.5f};
    const float searchRadius = segmentLength * 0.5f + outerWidth;
    const float chunkSize = GetChunkWorldSize();
    const uint32_t cells = GetChunkCells();
    const float cellSize = chunkSize / static_cast<float>(cells);

    bool changed = false;
    bool coverageChanged = false;
    std::vector<TERRAIN::TerrainChunkKey> touchedChunks{};
    std::set<TERRAIN::TerrainChunkKey> changedChunks{};
    ForEachChunkInBrushLocal(
        center, searchRadius, false,
        [&](const TERRAIN::TerrainChunkKey& key, TERRAIN::TerrainChunk& chunk, const MATH::Vec2& chunkOrigin) {
            touchedChunks.push_back(key);
            CaptureChunkBeforeEdit(key);
            const uint32_t stride = chunk.cells + 1u;
            const BrushVertexRange range =
                ResolveBrushVertexRange(center, searchRadius, chunkOrigin, cellSize, chunk.cells);
            if (!range.IsValid()) {
                return;
            }
            bool chunkChanged = false;
            for (uint32_t z = range.minZ; z <= range.maxZ; ++z) {
                for (uint32_t x = range.minX; x <= range.maxX; ++x) {
                    const float localX = chunkOrigin.x + static_cast<float>(x) * cellSize;
                    const float localZ = chunkOrigin.y + static_cast<float>(z) * cellSize;
                    const float fromStartX = localX - spec.start.x;
                    const float fromStartZ = localZ - spec.start.z;
                    const float along =
                        std::clamp((fromStartX * segmentX + fromStartZ * segmentZ) / segmentLengthSquared, 0.0f, 1.0f);
                    const float closestX = spec.start.x + segmentX * along;
                    const float closestZ = spec.start.z + segmentZ * along;
                    const float distanceX = localX - closestX;
                    const float distanceZ = localZ - closestZ;
                    const float distance = std::sqrt(distanceX * distanceX + distanceZ * distanceZ);
                    if (distance >= outerWidth) {
                        continue;
                    }
                    float mask = 1.0f;
                    if (distance > spec.halfWidth && spec.shoulderWidth > 1.0e-5f) {
                        const float edge =
                            std::clamp(1.0f - (distance - spec.halfWidth) / spec.shoulderWidth, 0.0f, 1.0f);
                        mask = edge * edge * (3.0f - 2.0f * edge);
                    }
                    const std::size_t index = static_cast<std::size_t>(z) * stride + x;
                    float& height = chunk.heights[index];
                    float& coverage = chunk.coverage[index];
                    const float oldHeight = height;
                    const float oldCoverage = coverage;
                    const float targetHeight = std::lerp(spec.start.y, spec.end.y, along);
                    height = std::lerp(height, targetHeight, spec.blendAmount * mask);
                    coverage = (std::max)(coverage, mask);
                    const bool vertexCoverageChanged = std::fabs(coverage - oldCoverage) > 1.0e-7f;
                    const bool vertexChanged = std::fabs(height - oldHeight) > 1.0e-7f || vertexCoverageChanged;
                    changed = changed || vertexChanged;
                    chunkChanged = chunkChanged || vertexChanged;
                    coverageChanged = coverageChanged || vertexCoverageChanged;
                    if (vertexCoverageChanged) {
                        MarkCoverageVertexDirty(key, x, z);
                    }
                }
            }
            if (chunkChanged) {
                changedChunks.insert(key);
            }
        });
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

bool TerrainSurfaceComponent::ApplyOpeningAtLocal(const TERRAIN::TerrainOpeningSpec& rawSpec) {
    const TERRAIN::TerrainOpeningSpec spec = TERRAIN::SanitizeTerrainOpeningSpec(rawSpec);
    MATH::Vec3 right = MATH::Cross({0.0f, 1.0f, 0.0f}, spec.normal);
    if (MATH::Dot(right, right) <= 1.0e-8f) {
        right = MATH::Cross({0.0f, 0.0f, 1.0f}, spec.normal);
    }
    if (MATH::Dot(right, right) <= 1.0e-8f) {
        return false;
    }
    right = MATH::Normalize(right);
    const MATH::Vec3 openingUp = MATH::Normalize(MATH::Cross(spec.normal, right));
    const float halfWidth = spec.width * 0.5f;
    const float halfHeight = spec.height * 0.5f;
    const float halfDepth = spec.depth * 0.5f;
    const float searchRadius = std::sqrt(halfWidth * halfWidth + halfHeight * halfHeight + halfDepth * halfDepth);
    const float chunkSize = GetChunkWorldSize();
    const uint32_t cells = GetChunkCells();
    const float cellSize = chunkSize / static_cast<float>(cells);

    bool changed = false;
    std::vector<TERRAIN::TerrainChunkKey> touchedChunks{};
    std::set<TERRAIN::TerrainChunkKey> changedChunks{};
    ForEachChunkInBrushLocal(
        spec.center, searchRadius, false,
        [&](const TERRAIN::TerrainChunkKey& key, TERRAIN::TerrainChunk& chunk, const MATH::Vec2& chunkOrigin) {
            touchedChunks.push_back(key);
            CaptureChunkBeforeEdit(key);
            const uint32_t stride = chunk.cells + 1u;
            const BrushVertexRange range =
                ResolveBrushVertexRange(spec.center, searchRadius, chunkOrigin, cellSize, chunk.cells);
            if (!range.IsValid()) {
                return;
            }
            bool chunkChanged = false;
            for (uint32_t z = range.minZ; z <= range.maxZ; ++z) {
                for (uint32_t x = range.minX; x <= range.maxX; ++x) {
                    const std::size_t index = static_cast<std::size_t>(z) * stride + x;
                    const MATH::Vec3 vertex{chunkOrigin.x + static_cast<float>(x) * cellSize, chunk.heights[index],
                                            chunkOrigin.y + static_cast<float>(z) * cellSize};
                    const MATH::Vec3 relative = vertex - spec.center;
                    const float localRight = std::fabs(MATH::Dot(relative, right));
                    const float localUp = std::fabs(MATH::Dot(relative, openingUp));
                    const float localDepth = std::fabs(MATH::Dot(relative, spec.normal));
                    const float edgeDistance =
                        (std::min)({halfWidth - localRight, halfHeight - localUp, halfDepth - localDepth});
                    if (edgeDistance < 0.0f) {
                        continue;
                    }
                    float openingWeight = 1.0f;
                    if (spec.feather > 1.0e-5f) {
                        const float t = std::clamp(edgeDistance / spec.feather, 0.0f, 1.0f);
                        openingWeight = t * t * (3.0f - 2.0f * t);
                    }
                    float& coverage = chunk.coverage[index];
                    const float oldCoverage = coverage;
                    coverage = std::lerp(coverage, 0.0f, openingWeight);
                    if (std::fabs(coverage - oldCoverage) <= 1.0e-7f) {
                        continue;
                    }
                    changed = true;
                    chunkChanged = true;
                    MarkCoverageVertexDirty(key, x, z);
                }
            }
            if (chunkChanged) {
                changedChunks.insert(key);
            }
        });
    if (!changed) {
        return false;
    }
    (void)StitchChunkBorders(touchedChunks, changedChunks);
    for (const TERRAIN::TerrainChunkKey& key : changedChunks) {
        MarkChunkChanged(key);
    }
    ++revision_;
    MarkGeometryChanged();
    ++coverageRevision_;
    NotifyRenderStateDirty();
    return true;
}

bool TerrainSurfaceComponent::PaintLayerAtWorld(std::string_view presetId, const MATH::Vec3& worldPos,
                                                float deltaSeconds) {
    return PaintLayerAtLocal(presetId, WorldToLocal(worldPos), deltaSeconds);
}

bool TerrainSurfaceComponent::PaintLayerAtLocal(std::string_view presetId, const MATH::Vec3& localPos,
                                                float deltaSeconds) {
    const TERRAIN::TerrainBrush brush = TERRAIN::SanitizeTerrainBrush(surface_.brush);
    const float dtScale = std::clamp(deltaSeconds, 0.0f, 1.0f);
    if (brush.strength <= 1.0e-6f || dtScale <= 0.0f) {
        return false;
    }
    TERRAIN::SurfaceLayerRegistry::Get().RegisterBuiltins();
    const int layerIndex = TERRAIN::SurfaceLayerRegistry::Get().ResolveIndex(presetId);
    if (layerIndex < 0 || static_cast<uint32_t>(layerIndex) >= TERRAIN::kTerrainChunkMaxLayers) {
        return false;
    }
    const uint32_t layer = static_cast<uint32_t>(layerIndex);

    // 層 preset の対応を記録する（着色 tint 解決用）。
    if (surface_.layerPresetIds.size() < static_cast<std::size_t>(layer) + 1u) {
        surface_.layerPresetIds.resize(static_cast<std::size_t>(layer) + 1u);
    }
    const std::string nextPresetId(presetId);
    bool changed = surface_.layerPresetIds[static_cast<std::size_t>(layer)] != nextPresetId;
    surface_.layerPresetIds[static_cast<std::size_t>(layer)] = nextPresetId;
    bool coverageChanged = false;

    const float chunkSize = GetChunkWorldSize();
    const uint32_t cells = GetChunkCells();
    const float cellSize = chunkSize / static_cast<float>(cells);

    std::vector<TERRAIN::TerrainChunkKey> touchedChunks{};
    std::set<TERRAIN::TerrainChunkKey> changedChunks{};
    ForEachChunkInBrushLocal(
        localPos, brush.radius, true,
        [&](const TERRAIN::TerrainChunkKey& key, TERRAIN::TerrainChunk& chunk, const MATH::Vec2& chunkOrigin) {
            touchedChunks.push_back(key);
            CaptureChunkBeforeEdit(key);
            bool chunkChanged = false;
            // 層数が足りなければ拡張する。
            if (chunk.layerCount <= layer) {
                if (!TERRAIN::EnsureTerrainChunkLayerCount(chunk, layer + 1u)) {
                    return;
                }
                changed = true;
                chunkChanged = true;
            }

            const uint32_t stride = chunk.cells + 1u;
            const std::size_t count = TERRAIN::TerrainChunkVertexCount(chunk.cells);
            VisitBrushVertices(localPos, brush, chunkOrigin, cellSize, chunk.cells, [&](const BrushVertex& point) {
                const auto x = point.x, z = point.z;
                const float falloff = point.falloff;
                const std::size_t cell = static_cast<std::size_t>(z) * stride + x;
                const float amount = BrushResponse(brush.strength, dtScale, falloff);
                const float oldCoverage = chunk.coverage[cell];
                const float oldTargetWeight = chunk.layerWeights[static_cast<std::size_t>(layer) * count + cell];
                // 対象層を増やし、全体で正規化する。
                chunk.layerWeights[static_cast<std::size_t>(layer) * count + cell] += amount;
                float total = 0.0f;
                for (uint32_t l = 0u; l < chunk.layerCount; ++l) {
                    total += chunk.layerWeights[static_cast<std::size_t>(l) * count + cell];
                }
                if (total > 1.0e-6f) {
                    for (uint32_t l = 0u; l < chunk.layerCount; ++l) {
                        chunk.layerWeights[static_cast<std::size_t>(l) * count + cell] /= total;
                    }
                }
                // 新規 chunk は coverage=0 から始め、筆の勾配で
                // 実体化する。四角い chunk 輪郭を露出させない。
                chunk.coverage[cell] = (std::max)(chunk.coverage[cell], falloff);
                const bool vertexCoverageChanged = std::fabs(chunk.coverage[cell] - oldCoverage) > 1.0e-7f;
                const bool vertexChanged =
                    std::fabs(chunk.layerWeights[static_cast<std::size_t>(layer) * count + cell] - oldTargetWeight) >
                        1.0e-7f ||
                    vertexCoverageChanged;
                changed = changed || vertexChanged;
                chunkChanged = chunkChanged || vertexChanged;
                coverageChanged = coverageChanged || vertexCoverageChanged;
                if (vertexCoverageChanged) {
                    MarkCoverageVertexDirty(key, x, z);
                }
            });
            if (chunkChanged) {
                changedChunks.insert(key);
            }
        },
        static_cast<int32_t>(layer));
    if (!changed) {
        return false;
    }
    coverageChanged = StitchChunkBorders(touchedChunks, changedChunks) || coverageChanged;
    for (const TERRAIN::TerrainChunkKey& key : changedChunks) {
        MarkChunkChanged(key);
    }
    ++revision_;
    if (coverageChanged) {
        MarkGeometryChanged();
        ++coverageRevision_;
    }
    NotifyRenderStateDirty();
    return true;
}

bool TerrainSurfaceComponent::PaintCoverageAtWorld(float target, const MATH::Vec3& worldPos, float deltaSeconds) {
    return PaintCoverageAtLocal(target, WorldToLocal(worldPos), deltaSeconds);
}

bool TerrainSurfaceComponent::PaintCoverageAtLocal(float target, const MATH::Vec3& localPos, float deltaSeconds) {
    const TERRAIN::TerrainBrush brush = TERRAIN::SanitizeTerrainBrush(surface_.brush);
    const float dtScale = std::clamp(deltaSeconds, 0.0f, 1.0f);
    if (brush.strength <= 1.0e-6f || dtScale <= 0.0f) {
        return false;
    }
    const float goal = std::clamp(target, 0.0f, 1.0f);
    const float chunkSize = GetChunkWorldSize();
    const uint32_t cells = GetChunkCells();
    const float cellSize = chunkSize / static_cast<float>(cells);

    bool changed = false;
    std::vector<TERRAIN::TerrainChunkKey> touchedChunks{};
    std::set<TERRAIN::TerrainChunkKey> changedChunks{};
    ForEachChunkInBrushLocal(
        localPos, brush.radius, goal > 1.0e-6f,
        [&](const TERRAIN::TerrainChunkKey& key, TERRAIN::TerrainChunk& chunk, const MATH::Vec2& chunkOrigin) {
            touchedChunks.push_back(key);
            CaptureChunkBeforeEdit(key);
            const uint32_t stride = chunk.cells + 1u;
            bool chunkChanged = false;
            VisitBrushVertices(localPos, brush, chunkOrigin, cellSize, chunk.cells, [&](const BrushVertex& point) {
                const auto x = point.x, z = point.z;
                const float falloff = point.falloff;
                float& c = chunk.coverage[static_cast<std::size_t>(z) * stride + x];
                const float oldCoverage = c;
                c += (goal - c) * BrushResponse(brush.strength, dtScale, falloff);
                const bool vertexCoverageChanged = std::fabs(c - oldCoverage) > 1.0e-7f;
                changed = changed || vertexCoverageChanged;
                chunkChanged = chunkChanged || vertexCoverageChanged;
                if (vertexCoverageChanged) {
                    MarkCoverageVertexDirty(key, x, z);
                }
            });
            if (chunkChanged) {
                changedChunks.insert(key);
            }
        });
    if (!changed) {
        return false;
    }
    (void)StitchChunkBorders(touchedChunks, changedChunks);
    for (const TERRAIN::TerrainChunkKey& key : changedChunks) {
        MarkChunkChanged(key);
    }
    ++revision_;
    MarkGeometryChanged();
    ++coverageRevision_;
    NotifyRenderStateDirty();
    return true;
}

} // namespace HIKARI
