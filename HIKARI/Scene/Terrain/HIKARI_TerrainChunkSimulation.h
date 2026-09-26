#pragma once

#include "Scene/Terrain/HIKARI_TerrainSurfaceQueryService.h"
#include "Scene/Terrain/HIKARI_TerrainInteraction.h"
#include "Scene/Terrain/HIKARI_TerrainSurface.h"

namespace HIKARI::TERRAIN {
// シミュレーションと境界縫合は同じ休眠閾値を使う。
inline constexpr float kRecoverySnapEpsilon = 1.5e-3f;
inline constexpr float kDeformableMaskClearThreshold = 1.0e-3f;

void SyncChunkFromAuthored(TERRAIN::TerrainChunkRuntimeState& chunk, const TERRAIN::TerrainChunk& authored,
                           const TERRAIN::TerrainSurface& surface);
void RebuildCombinedHeights(TERRAIN::TerrainChunkRuntimeState& chunk);
void RebuildCombinedHeightsInRegion(TERRAIN::TerrainChunkRuntimeState& chunk, uint32_t minimumX, uint32_t minimumZ,
                                    uint32_t maximumX, uint32_t maximumZ);
bool ApplyStylizedImprint(TERRAIN::TerrainChunkRuntimeState& chunk, TERRAIN::TerrainFootprintKind footprintKind,
                          float localX, float localZ, float localRadiusX, float localRadiusZ, float localVerticalRadius,
                          const MATH::Vec2& localTangent, const MATH::Vec2& localBitangent, float edgeSoftness,
                          float sinkDepth, bool holdOnly);
bool RelaxChunk(TERRAIN::TerrainChunkRuntimeState& chunk, float fixedDt);
bool FootprintBasisUsable(const MATH::Vec2& tangent, const MATH::Vec2& bitangent) noexcept;
bool EvaluateFootprintFactor(TERRAIN::TerrainFootprintKind kind, float dx, float dz, const MATH::Vec2& tangent,
                             const MATH::Vec2& bitangent, float radiusX, float radiusZ, float& outFactor) noexcept;
} // namespace HIKARI::TERRAIN
