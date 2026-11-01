#include "Scene/Terrain/HIKARI_TerrainChunkSimulation.h"

#include <algorithm>
#include <cmath>
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceResponse.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceConversion.h"

namespace HIKARI::TERRAIN {
bool FootprintBasisUsable(const MATH::Vec2& tangent, const MATH::Vec2& bitangent) noexcept {
    const float tangentLengthSq = tangent.x * tangent.x + tangent.y * tangent.y;
    const float bitangentLengthSq = bitangent.x * bitangent.x + bitangent.y * bitangent.y;
    const float determinant = tangent.x * bitangent.y - tangent.y * bitangent.x;
    const float basisScale = std::sqrt(tangentLengthSq * bitangentLengthSq);
    return std::isfinite(basisScale) && std::isfinite(determinant) && basisScale > 1.0e-8f &&
           std::fabs(determinant) > 1.0e-5f * basisScale;
}

bool EvaluateFootprintFactor(TERRAIN::TerrainFootprintKind kind, float dx, float dz, const MATH::Vec2& tangent,
                             const MATH::Vec2& bitangent, float radiusX, float radiusZ, float& outFactor) noexcept {
    outFactor = 0.0f;
    if (!FootprintBasisUsable(tangent, bitangent)) {
        return false;
    }
    const float determinant = tangent.x * bitangent.y - tangent.y * bitangent.x;
    // local XZ の差分を、world footprint の tangent/bitangent 単位へ
    // 戻す。非一様 scale 後の二軸は直交しないため dot では解けない。
    const float along = (dx * bitangent.y - dz * bitangent.x) / determinant;
    const float across = (tangent.x * dz - tangent.y * dx) / determinant;
    const float safeZ = (std::max)(radiusZ, 0.01f);
    const float safeX =
        kind == TERRAIN::TerrainFootprintKind::Capsule ? (std::max)(radiusX, safeZ) : (std::max)(radiusX, 0.01f);
    if (kind == TERRAIN::TerrainFootprintKind::Capsule) {
        const float capRadius = safeZ;
        const float segmentHalf = (std::max)(safeX - capRadius, 0.0f);
        const float axial = (std::max)(std::fabs(along) - segmentHalf, 0.0f);
        outFactor = std::sqrt(axial * axial + across * across) / capRadius;
        return std::isfinite(outFactor);
    }
    outFactor = std::sqrt(along * along / (safeX * safeX) + across * across / (safeZ * safeZ));
    return std::isfinite(outFactor);
}

namespace {
constexpr float kRelaxationIntervalSeconds = 0.125f;
CPU_PROFILE::DynamicPassHandle TerrainRelaxTiming() {
    static const CPU_PROFILE::DynamicPassHandle handle = CPU_PROFILE::RegisterDynamicPass("Terrain/RelaxChunk");
    return handle;
}

float CombineVertex(TerrainChunkRuntimeState& chunk, std::size_t index, float minimum, float maximum) {
    const auto channels = TERRAIN::ClampTerrainSurfaceDisplacementChannels(
        {chunk.elasticHeights[index], chunk.plasticHeights[index]}, minimum, maximum);
    chunk.elasticHeights[index] = channels.elastic;
    chunk.plasticHeights[index] = channels.plastic;
    const float deformation = channels.Combined();
    chunk.deformHeights[index] = EvaluateTerrainChunkFinalHeight(0.0f, deformation, chunk.deformableMask[index],
                                                                 chunk.settings.deformationEnabled);
    chunk.heights[index] = EvaluateTerrainChunkFinalHeight(
        chunk.baseHeights[index], deformation, chunk.deformableMask[index], chunk.settings.deformationEnabled);
    return deformation;
}

} // namespace

void SyncChunkFromAuthored(TERRAIN::TerrainChunkRuntimeState& chunk, const TERRAIN::TerrainChunk& authored,
                           const TERRAIN::TerrainSurface& surface) {
    // authored 格子を runtime response 格子へ双線形投影する。
    // runtime deformation は別配列なので、筆刷更新で痕跡を混ぜない。
    const uint32_t sourceCells = (std::max)(authored.cells, 1u);
    const uint32_t sourceStride = sourceCells + 1u;
    const uint32_t targetCells = (std::max)(chunk.cells, 1u);
    const uint32_t targetStride = targetCells + 1u;
    const std::size_t sourceCount = static_cast<std::size_t>(sourceStride) * sourceStride;
    const std::size_t targetCount = static_cast<std::size_t>(targetStride) * targetStride;
    chunk.baseHeights.assign(targetCount, 0.0f);
    if (authored.heights.size() == sourceCount) {
        const auto source = [&](uint32_t x, uint32_t z) {
            return authored.heights[static_cast<std::size_t>(z) * sourceStride + x];
        };
        for (uint32_t z = 0u; z <= targetCells; ++z) {
            const float fz = static_cast<float>(z) * static_cast<float>(sourceCells) / static_cast<float>(targetCells);
            const uint32_t z0 = static_cast<uint32_t>(fz);
            const uint32_t z1 = (std::min)(z0 + 1u, sourceCells);
            const float tz = fz - static_cast<float>(z0);
            for (uint32_t x = 0u; x <= targetCells; ++x) {
                const float fx =
                    static_cast<float>(x) * static_cast<float>(sourceCells) / static_cast<float>(targetCells);
                const uint32_t x0 = static_cast<uint32_t>(fx);
                const uint32_t x1 = (std::min)(x0 + 1u, sourceCells);
                const float tx = fx - static_cast<float>(x0);
                chunk.baseHeights[static_cast<std::size_t>(z) * targetStride + x] = std::lerp(
                    std::lerp(source(x0, z0), source(x1, z0), tx), std::lerp(source(x0, z1), source(x1, z1), tx), tz);
            }
        }
    }
    TERRAIN::ProjectTerrainChunkDeformableMask(surface, authored, targetCells, chunk.deformableMask);
    // 完全な hard layer に隠れた response を残すと、後から sand を塗った
    // ときに復活する。混合境界の feather は保ち、ほぼ 0 の内部だけ消す。
    const std::size_t responseCount = chunk.deformableMask.size();
    if (chunk.elasticHeights.size() == responseCount && chunk.plasticHeights.size() == responseCount &&
        chunk.recoveryHold.size() == responseCount) {
        for (std::size_t index = 0u; index < responseCount; ++index) {
            if (chunk.deformableMask[index] <= kDeformableMaskClearThreshold) {
                chunk.elasticHeights[index] = 0.0f;
                chunk.plasticHeights[index] = 0.0f;
                chunk.recoveryHold[index] = 0.0f;
            }
        }
        chunk.hasDeformation = std::any_of(chunk.elasticHeights.begin(), chunk.elasticHeights.end(),
                                           [](float value) { return std::fabs(value) > kRecoverySnapEpsilon; }) ||
                               std::any_of(chunk.plasticHeights.begin(), chunk.plasticHeights.end(),
                                           [](float value) { return std::fabs(value) > kRecoverySnapEpsilon; });
        chunk.needsRelaxation = chunk.hasDeformation;
    }
    RebuildCombinedHeights(chunk);
    ++chunk.stateRevision;
}

void RebuildCombinedHeights(TERRAIN::TerrainChunkRuntimeState& chunk) {
    const std::size_t count = chunk.baseHeights.size();
    if (chunk.elasticHeights.size() != count) {
        chunk.elasticHeights.assign(count, 0.0f);
    }
    if (chunk.plasticHeights.size() != count) {
        chunk.plasticHeights.assign(count, 0.0f);
    }
    if (chunk.recoveryHold.size() != count) {
        chunk.recoveryHold.assign(count, 0.0f);
    }
    if (chunk.elasticScratch.size() != count) {
        chunk.elasticScratch.assign(count, 0.0f);
    }
    if (chunk.deformHeights.size() != count) {
        chunk.deformHeights.assign(count, 0.0f);
    }
    if (chunk.heights.size() != count) {
        chunk.heights.resize(count);
    }
    if (chunk.deformableMask.size() != count) {
        // Refresh が authored layer weights を投影するまで layer mask は無い。
        // それまでは全体を変形可能として扱う。
        chunk.deformableMask.assign(count, 1.0f);
    }
    const float minimum = -(std::max)(chunk.settings.maxDepth, 0.0f);
    const float maximum = (std::max)(chunk.settings.maxDepth, 0.0f) * (std::max)(chunk.settings.rimStrength, 0.0f);
    bool hasDeformation = false;
    bool activeBoundsValid = false;
    uint32_t activeMinimumX = (std::max)(chunk.cells, 1u);
    uint32_t activeMinimumZ = activeMinimumX;
    uint32_t activeMaximumX = 0u;
    uint32_t activeMaximumZ = 0u;
    const uint32_t stride = (std::max)(chunk.cells, 1u) + 1u;
    for (std::size_t i = 0u; i < count; ++i) {
        const float rawDeformation = CombineVertex(chunk, i, minimum, maximum);
        hasDeformation = hasDeformation || std::fabs(rawDeformation) > kRecoverySnapEpsilon;
        if (std::fabs(rawDeformation) > kRecoverySnapEpsilon || chunk.recoveryHold[i] > 0.0f) {
            const uint32_t x = static_cast<uint32_t>(i % stride);
            const uint32_t z = static_cast<uint32_t>(i / stride);
            activeBoundsValid = true;
            activeMinimumX = (std::min)(activeMinimumX, x);
            activeMinimumZ = (std::min)(activeMinimumZ, z);
            activeMaximumX = (std::max)(activeMaximumX, x);
            activeMaximumZ = (std::max)(activeMaximumZ, z);
        }
    }
    chunk.hasDeformation = hasDeformation;
    chunk.needsRelaxation = hasDeformation;
    chunk.activeBoundsValid = activeBoundsValid;
    if (activeBoundsValid) {
        chunk.activeMinimumX = activeMinimumX;
        chunk.activeMinimumZ = activeMinimumZ;
        chunk.activeMaximumX = activeMaximumX;
        chunk.activeMaximumZ = activeMaximumZ;
    } else {
        chunk.relaxationAccumulatorSeconds = 0.0f;
    }
}

void RebuildCombinedHeightsInRegion(TERRAIN::TerrainChunkRuntimeState& chunk, uint32_t minimumX, uint32_t minimumZ,
                                    uint32_t maximumX, uint32_t maximumZ) {
    const uint32_t cells = (std::max)(chunk.cells, 1u);
    const uint32_t stride = cells + 1u;
    const std::size_t count = static_cast<std::size_t>(stride) * stride;
    if (chunk.baseHeights.size() != count || chunk.elasticHeights.size() != count ||
        chunk.plasticHeights.size() != count || chunk.deformHeights.size() != count || chunk.heights.size() != count ||
        chunk.deformableMask.size() != count) {
        RebuildCombinedHeights(chunk);
        return;
    }
    minimumX = (std::min)(minimumX, cells);
    minimumZ = (std::min)(minimumZ, cells);
    maximumX = (std::min)(maximumX, cells);
    maximumZ = (std::min)(maximumZ, cells);
    if (minimumX > maximumX || minimumZ > maximumZ) {
        return;
    }

    const float minimum = -(std::max)(chunk.settings.maxDepth, 0.0f);
    const float maximum = (std::max)(chunk.settings.maxDepth, 0.0f) * (std::max)(chunk.settings.rimStrength, 0.0f);
    for (uint32_t z = minimumZ; z <= maximumZ; ++z) {
        for (uint32_t x = minimumX; x <= maximumX; ++x) {
            const std::size_t index = static_cast<std::size_t>(z) * stride + x;

            CombineVertex(chunk, index, minimum, maximum);
        }
    }
}

bool ApplyStylizedImprint(TERRAIN::TerrainChunkRuntimeState& chunk, TERRAIN::TerrainFootprintKind footprintKind,
                          float localX, float localZ, float localRadiusX, float localRadiusZ, float localVerticalRadius,
                          const MATH::Vec2& localTangent, const MATH::Vec2& localBitangent, float edgeSoftness,
                          float sinkDepth, bool holdOnly) {
    const uint32_t cells = (std::max)(chunk.cells, 1u);
    const uint32_t stride = cells + 1u;
    const std::size_t vertexCount = static_cast<std::size_t>(stride) * stride;
    if (chunk.deformHeights.size() != vertexCount || chunk.elasticHeights.size() != vertexCount ||
        chunk.plasticHeights.size() != vertexCount || chunk.recoveryHold.size() != vertexCount ||
        chunk.coverage.size() != vertexCount || (!holdOnly && sinkDepth <= 0.0f)) {
        return false;
    }

    const float radiusZ = (std::max)(localRadiusZ, 0.01f);
    const float radiusX = footprintKind == TERRAIN::TerrainFootprintKind::Capsule ? (std::max)(localRadiusX, radiusZ)
                                                                                  : (std::max)(localRadiusX, 0.01f);
    if (!FootprintBasisUsable(localTangent, localBitangent)) {
        return false;
    }
    const float verticalRadius = (std::max)(localVerticalRadius, 0.01f);
    float depth = (std::min)(sinkDepth, chunk.settings.maxDepth);
    if (footprintKind == TERRAIN::TerrainFootprintKind::Sphere) {
        depth = (std::min)(depth, 2.0f * verticalRadius);
    }
    if (depth <= 0.0f && !holdOnly) {
        return false;
    }

    const float capFactor =
        holdOnly ? 1.0f
                 : (footprintKind == TERRAIN::TerrainFootprintKind::Sphere
                        ? std::sqrt((std::max)(2.0f * verticalRadius * depth - depth * depth, 0.0f)) / verticalRadius
                        : 1.0f);
    const float cell = (std::max)(chunk.cellSize, 1.0e-4f);
    const float localRadiusXLength =
        radiusX * std::sqrt(localTangent.x * localTangent.x + localTangent.y * localTangent.y);
    const float localRadiusZLength =
        radiusZ * std::sqrt(localBitangent.x * localBitangent.x + localBitangent.y * localBitangent.y);
    const float rimBandFactor = (std::max)({chunk.settings.rimWidthScale, cell / (std::max)(localRadiusXLength, 0.01f),
                                            cell / (std::max)(localRadiusZLength, 0.01f)});
    const float reachFactor = capFactor + rimBandFactor;
    const float minimum = -chunk.settings.maxDepth;
    const float maximum = chunk.settings.maxDepth * chunk.settings.rimStrength;

    const float reachX = (std::fabs(localTangent.x) * radiusX + std::fabs(localBitangent.x) * radiusZ) * reachFactor;
    const float reachZ = (std::fabs(localTangent.y) * radiusX + std::fabs(localBitangent.y) * radiusZ) * reachFactor;
    const int32_t minX =
        std::clamp(static_cast<int32_t>(std::floor((localX - reachX - chunk.chunkOriginLocal.x) / cell)), 0,
                   static_cast<int32_t>(cells));
    const int32_t maxX =
        std::clamp(static_cast<int32_t>(std::ceil((localX + reachX - chunk.chunkOriginLocal.x) / cell)), 0,
                   static_cast<int32_t>(cells));
    const int32_t minZ =
        std::clamp(static_cast<int32_t>(std::floor((localZ - reachZ - chunk.chunkOriginLocal.y) / cell)), 0,
                   static_cast<int32_t>(cells));
    const int32_t maxZ =
        std::clamp(static_cast<int32_t>(std::ceil((localZ + reachZ - chunk.chunkOriginLocal.y) / cell)), 0,
                   static_cast<int32_t>(cells));

    bool changed = false;
    bool holdTouched = false;
    for (int32_t z = minZ; z <= maxZ; ++z) {
        for (int32_t x = minX; x <= maxX; ++x) {
            const std::size_t vertexIndex = static_cast<std::size_t>(z) * stride + static_cast<uint32_t>(x);
            if (chunk.coverage[vertexIndex] < TERRAIN::kTerrainSurfaceVisibleCoverageThreshold) {
                continue;
            }
            const float deformableMask = vertexIndex < chunk.deformableMask.size()
                                             ? std::clamp(chunk.deformableMask[vertexIndex], 0.0f, 1.0f)
                                             : 1.0f;
            if (!std::isfinite(deformableMask) || deformableMask <= kDeformableMaskClearThreshold) {
                // RebuildCombinedHeights が final response へ mask を一度だけ
                // 適用する。ここでは 0 領域の gate だけにし、imprint にも
                // 掛けると混合境界を二重に mask してしまう。
                continue;
            }
            const float vx = chunk.chunkOriginLocal.x + static_cast<float>(x) * cell;
            const float vz = chunk.chunkOriginLocal.y + static_cast<float>(z) * cell;
            const float dx = vx - localX;
            const float dz = vz - localZ;
            float radiusFactor = 0.0f;
            if (!EvaluateFootprintFactor(footprintKind, dx, dz, localTangent, localBitangent, radiusX, radiusZ,
                                         radiusFactor)) {
                continue;
            }
            if (radiusFactor > reachFactor) {
                continue;
            }

            // sleeping Dynamic / 停止 Kinematic は active contact を
            // 保持するだけで、新しい塑性量を追加しない。
            const float previousHold = chunk.recoveryHold[vertexIndex];
            chunk.recoveryHold[vertexIndex] = (std::max)(previousHold, chunk.settings.recoveryDelaySeconds);
            holdTouched = holdTouched || chunk.recoveryHold[vertexIndex] != previousHold;
            if (holdOnly) {
                continue;
            }

            float imprint = 0.0f;
            if (radiusFactor <= capFactor) {
                if (footprintKind == TERRAIN::TerrainFootprintKind::Sphere) {
                    imprint = (std::max)(0.0f, verticalRadius *
                                                       std::sqrt((std::max)(1.0f - radiusFactor * radiusFactor, 0.0f)) -
                                                   (verticalRadius - depth));
                } else {
                    const float softness = std::clamp(edgeSoftness, 0.0f, 1.0f);
                    const float exponent = std::lerp(2.2f, 0.55f, softness);
                    imprint = depth * std::pow((std::max)(1.0f - radiusFactor * radiusFactor, 0.0f), exponent);
                }
            } else if (rimBandFactor > 0.0f) {
                const float t = (radiusFactor - capFactor) / rimBandFactor;
                imprint = -chunk.settings.rimStrength * depth * 0.5f * (1.0f - t) * (1.0f - t);
            }

            float& elastic = chunk.elasticHeights[vertexIndex];
            float& plastic = chunk.plasticHeights[vertexIndex];
            const float height = elastic + plastic;
            const float desired = std::clamp(-imprint, minimum, maximum);
            const float next = TERRAIN::AccumulateTerrainSurfaceDisplacement(
                height, desired,
                imprint >= 0.0f ? TERRAIN::TerrainSurfaceDisplacementKind::Depression
                                : TERRAIN::TerrainSurfaceDisplacementKind::Rim);
            if (std::fabs(next - height) > 1.0e-6f) {
                const float delta = next - height;
                const float plasticShare = std::clamp(chunk.settings.plasticity, 0.0f, 1.0f);
                const float targetPlastic = next * plasticShare;
                const float plasticDelta = delta < 0.0f ? std::clamp(targetPlastic - plastic, delta, 0.0f)
                                                        : std::clamp(targetPlastic - plastic, 0.0f, delta);
                plastic += plasticDelta;
                elastic += delta - plasticDelta;
                changed = true;
            }
        }
    }
    if (changed || holdTouched) {
        const uint32_t boundsMinimumX = static_cast<uint32_t>(minX);
        const uint32_t boundsMinimumZ = static_cast<uint32_t>(minZ);
        const uint32_t boundsMaximumX = static_cast<uint32_t>(maxX);
        const uint32_t boundsMaximumZ = static_cast<uint32_t>(maxZ);
        if (!chunk.activeBoundsValid) {
            chunk.activeMinimumX = boundsMinimumX;
            chunk.activeMinimumZ = boundsMinimumZ;
            chunk.activeMaximumX = boundsMaximumX;
            chunk.activeMaximumZ = boundsMaximumZ;
            chunk.activeBoundsValid = true;
        } else {
            chunk.activeMinimumX = (std::min)(chunk.activeMinimumX, boundsMinimumX);
            chunk.activeMinimumZ = (std::min)(chunk.activeMinimumZ, boundsMinimumZ);
            chunk.activeMaximumX = (std::max)(chunk.activeMaximumX, boundsMaximumX);
            chunk.activeMaximumZ = (std::max)(chunk.activeMaximumZ, boundsMaximumZ);
        }
    }
    if (changed) {
        chunk.responseGridActive = true;
        chunk.hasDeformation = true;
        chunk.needsRelaxation = true;
        RebuildCombinedHeightsInRegion(chunk, static_cast<uint32_t>(minX), static_cast<uint32_t>(minZ),
                                       static_cast<uint32_t>(maxX), static_cast<uint32_t>(maxZ));
        ++chunk.stateRevision;
    }
    return changed;
}

bool RelaxChunk(TERRAIN::TerrainChunkRuntimeState& chunk, float fixedDt) {
    if (!chunk.hasDeformation) {
        chunk.needsRelaxation = false;
        chunk.activeBoundsValid = false;
        chunk.relaxationAccumulatorSeconds = 0.0f;
        return false;
    }
    CPU_PROFILE::ScopedDynamicCpuTimer relaxTimer(TerrainRelaxTiming());
    const uint32_t cells = (std::max)(chunk.cells, 1u);
    const uint32_t stride = cells + 1u;
    const std::size_t vertexCount = static_cast<std::size_t>(stride) * stride;
    if (chunk.deformHeights.size() != vertexCount || chunk.elasticHeights.size() != vertexCount ||
        chunk.plasticHeights.size() != vertexCount || chunk.recoveryHold.size() != vertexCount ||
        chunk.elasticScratch.size() != vertexCount) {
        return false;
    }

    if (!chunk.activeBoundsValid) {
        const bool lifecycleChanged = chunk.hasDeformation;
        chunk.hasDeformation = false;
        chunk.needsRelaxation = false;
        chunk.relaxationAccumulatorSeconds = 0.0f;
        if (lifecycleChanged) {
            ++chunk.stateRevision;
        }
        return lifecycleChanged;
    }

    // moving imprint は 60 Hz で適用する一方、回復/拡散は蓄積 dt を
    // 8 Hz で処理する。視覚変形の追従を保ちつつ dormant contact の
    // fixed cost を 1/7.5 程度へ落とす。
    const float safeFixedDt = (std::max)(fixedDt, 0.0f);
    chunk.relaxationAccumulatorSeconds = (std::min)(chunk.relaxationAccumulatorSeconds + safeFixedDt, 0.5f);
    if (chunk.relaxationAccumulatorSeconds + 1.0e-6f < kRelaxationIntervalSeconds) {
        return false;
    }
    const float safeDt = chunk.relaxationAccumulatorSeconds;
    chunk.relaxationAccumulatorSeconds = 0.0f;

    const uint32_t activeMinX = (std::min)(chunk.activeMinimumX, cells);
    const uint32_t activeMinZ = (std::min)(chunk.activeMinimumZ, cells);
    const uint32_t activeMaxX = (std::min)(chunk.activeMaximumX, cells);
    const uint32_t activeMaxZ = (std::min)(chunk.activeMaximumZ, cells);

    const auto expandMinimum = [](uint32_t value, uint32_t amount) { return value > amount ? value - amount : 0u; };
    const auto expandMaximum = [cells](uint32_t value, uint32_t amount) { return (std::min)(cells, value + amount); };
    const uint32_t workMinX = expandMinimum(activeMinX, 1u);
    const uint32_t workMinZ = expandMinimum(activeMinZ, 1u);
    const uint32_t workMaxX = expandMaximum(activeMaxX, 1u);
    const uint32_t workMaxZ = expandMaximum(activeMaxZ, 1u);

    bool changed = false;
    const float elasticKeep = std::exp(-(std::max)(chunk.settings.refillPerSecond, 0.0f) * safeDt);
    const float plasticKeep = std::exp(-(std::max)(chunk.settings.plasticRefillPerSecond, 0.0f) * safeDt);
    const auto maskAt = [&](std::size_t index) noexcept {
        if (chunk.deformableMask.size() != vertexCount) {
            return 1.0f;
        }
        const float value = chunk.deformableMask[index];
        return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
    };
    const auto snapRecovered = [](float value) noexcept {
        return std::isfinite(value) && std::fabs(value) > kRecoverySnapEpsilon ? value : 0.0f;
    };
    for (uint32_t z = workMinZ; z <= workMaxZ; ++z) {
        for (uint32_t x = workMinX; x <= workMaxX; ++x) {
            const std::size_t index = static_cast<std::size_t>(z) * stride + x;
            float& elastic = chunk.elasticHeights[index];
            float& plastic = chunk.plasticHeights[index];
            if (maskAt(index) <= kDeformableMaskClearThreshold) {
                changed = changed || std::fabs(elastic) > kRecoverySnapEpsilon ||
                          std::fabs(plastic) > kRecoverySnapEpsilon || chunk.recoveryHold[index] > 0.0f;
                elastic = 0.0f;
                plastic = 0.0f;
                chunk.recoveryHold[index] = 0.0f;
                continue;
            }
            const float snappedElastic = snapRecovered(elastic);
            const float snappedPlastic = snapRecovered(plastic);
            changed = changed || snappedElastic != elastic || snappedPlastic != plastic;
            elastic = snappedElastic;
            plastic = snappedPlastic;

            float& hold = chunk.recoveryHold[index];
            if (hold > 0.0f) {
                const float nextHold = (std::max)(hold - safeDt, 0.0f);
                hold = nextHold;
                continue;
            }
            const float nextElastic = snapRecovered(elastic * elasticKeep);
            const float nextPlastic = snapRecovered(plastic * plasticKeep);
            changed = changed || nextElastic != elastic || nextPlastic != plastic;
            elastic = nextElastic;
            plastic = nextPlastic;
        }
    }

    const float diffusion =
        std::clamp(1.0f - std::exp(-(std::max)(chunk.settings.diffusionPerSecond, 0.0f) * safeDt), 0.0f, 0.25f);
    bool hasElastic = false;
    for (uint32_t z = workMinZ; z <= workMaxZ && !hasElastic; ++z) {
        for (uint32_t x = workMinX; x <= workMaxX; ++x) {
            const std::size_t index = static_cast<std::size_t>(z) * stride + x;
            if (std::fabs(chunk.elasticHeights[index]) > kRecoverySnapEpsilon) {
                hasElastic = true;
                break;
            }
        }
    }
    if (diffusion > 1.0e-4f && hasElastic) {
        // work bounds の cell が読む 4-neighbor までを snapshot する。
        // elasticScratch は再利用 buffer なので、この halo 外の古い値を
        // diffusion source として参照しない。
        const uint32_t copyMinX = expandMinimum(workMinX, 1u);
        const uint32_t copyMinZ = expandMinimum(workMinZ, 1u);
        const uint32_t copyMaxX = expandMaximum(workMaxX, 1u);
        const uint32_t copyMaxZ = expandMaximum(workMaxZ, 1u);
        for (uint32_t z = copyMinZ; z <= copyMaxZ; ++z) {
            const std::size_t begin = static_cast<std::size_t>(z) * stride + copyMinX;
            const std::size_t end = static_cast<std::size_t>(z) * stride + copyMaxX + 1u;
            std::copy(chunk.elasticHeights.begin() + begin, chunk.elasticHeights.begin() + end,
                      chunk.elasticScratch.begin() + begin);
        }
        const std::vector<float>& source = chunk.elasticScratch;
        const float keep = 1.0f - diffusion;
        const float neighbor = diffusion * 0.25f;
        const uint32_t diffusionMinX = (std::max)(workMinX, 1u);
        const uint32_t diffusionMinZ = (std::max)(workMinZ, 1u);
        const uint32_t diffusionMaxX = (std::min)(workMaxX, cells - 1u);
        const uint32_t diffusionMaxZ = (std::min)(workMaxZ, cells - 1u);
        for (uint32_t z = diffusionMinZ; z <= diffusionMaxZ; ++z) {
            for (uint32_t x = diffusionMinX; x <= diffusionMaxX; ++x) {
                const std::size_t index = static_cast<std::size_t>(z) * stride + x;
                if (chunk.recoveryHold[index] > 0.0f) {
                    continue;
                }
                const auto neighborValue = [&](std::size_t candidate) {
                    return maskAt(candidate) <= kDeformableMaskClearThreshold || chunk.recoveryHold[candidate] > 0.0f
                               ? source[index]
                               : source[candidate];
                };
                float next = source[index] * keep + (neighborValue(index - stride) + neighborValue(index + stride) +
                                                     neighborValue(index - 1u) + neighborValue(index + 1u)) *
                                                        neighbor;
                if (!std::isfinite(next) || std::fabs(next) <= kRecoverySnapEpsilon) {
                    next = 0.0f;
                }
                changed = changed || next != chunk.elasticHeights[index];
                chunk.elasticHeights[index] = next;
            }
        }
    }
    const float minimum = -(std::max)(chunk.settings.maxDepth, 0.0f);
    const float maximum = (std::max)(chunk.settings.maxDepth, 0.0f) * (std::max)(chunk.settings.rimStrength, 0.0f);
    bool remains = false;
    bool nextBoundsValid = false;
    uint32_t nextMinimumX = cells;
    uint32_t nextMinimumZ = cells;
    uint32_t nextMaximumX = 0u;
    uint32_t nextMaximumZ = 0u;
    for (uint32_t z = workMinZ; z <= workMaxZ; ++z) {
        for (uint32_t x = workMinX; x <= workMaxX; ++x) {
            const std::size_t index = static_cast<std::size_t>(z) * stride + x;
            TERRAIN::TerrainSurfaceDisplacementChannels channels = TERRAIN::ClampTerrainSurfaceDisplacementChannels(
                {
                    chunk.elasticHeights[index],
                    chunk.plasticHeights[index],
                },
                minimum, maximum);
            if (chunk.recoveryHold[index] <= 0.0f) {
                // chunk 全体ではなく vertex ごとに lifecycle を閉じる。
                // seam の両側は同じ hold を共有するため、一方だけが elastic、
                // 他方だけが plastic になる状態を作らない。
                const float combined = channels.Combined();
                const float frozenResponse = std::fabs(combined) > kRecoverySnapEpsilon ? combined : 0.0f;
                channels = {0.0f, frozenResponse};
            }
            changed = changed || channels.elastic != chunk.elasticHeights[index] ||
                      channels.plastic != chunk.plasticHeights[index];
            chunk.elasticHeights[index] = channels.elastic;
            chunk.plasticHeights[index] = channels.plastic;
            const bool vertexRemains = std::fabs(channels.elastic) > kRecoverySnapEpsilon ||
                                       std::fabs(channels.plastic) > kRecoverySnapEpsilon;
            remains = remains || vertexRemains;
            if (vertexRemains) {
                nextBoundsValid = true;
                nextMinimumX = (std::min)(nextMinimumX, x);
                nextMinimumZ = (std::min)(nextMinimumZ, z);
                nextMaximumX = (std::max)(nextMaximumX, x);
                nextMaximumZ = (std::max)(nextMaximumZ, z);
            }
        }
    }
    const bool lifecycleChanged = chunk.hasDeformation != remains;
    if (changed) {
        RebuildCombinedHeightsInRegion(chunk, workMinX, workMinZ, workMaxX, workMaxZ);
    }
    chunk.hasDeformation = remains;
    chunk.needsRelaxation = remains;
    chunk.activeBoundsValid = nextBoundsValid;
    if (nextBoundsValid) {
        chunk.activeMinimumX = nextMinimumX;
        chunk.activeMinimumZ = nextMinimumZ;
        chunk.activeMaximumX = nextMaximumX;
        chunk.activeMaximumZ = nextMaximumZ;
    } else {
        chunk.relaxationAccumulatorSeconds = 0.0f;
    }
    if (changed || lifecycleChanged) {
        ++chunk.stateRevision;
        return true;
    }
    return false;
}

} // namespace HIKARI::TERRAIN
