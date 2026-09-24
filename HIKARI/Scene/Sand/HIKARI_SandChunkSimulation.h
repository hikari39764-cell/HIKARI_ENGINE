#pragma once

#include "Scene/Sand/HIKARI_SandSurfaceQueryService.h"
#include "Scene/Terrain/HIKARI_TerrainInteraction.h"
#include "Scene/Terrain/HIKARI_TerrainSurface.h"

namespace HIKARI::SAND {
// シミュレーションと境界縫合は同じ休眠閾値を使う。
inline constexpr float kRecoverySnapEpsilon = 1.5e-3f;
inline constexpr float kDeformableMaskClearThreshold = 1.0e-3f;

void SyncChunkFromAuthored(SAND::SandChunkRuntimeState& chunk, const TERRAIN::TerrainChunk& authored,
                           const TERRAIN::TerrainSurface& surface);
void RebuildCombinedHeights(SAND::SandChunkRuntimeState& chunk);
void RebuildCombinedHeightsInRegion(SAND::SandChunkRuntimeState& chunk, uint32_t minimumX, uint32_t minimumZ,
                                    uint32_t maximumX, uint32_t maximumZ);
bool ApplyStylizedImprint(SAND::SandChunkRuntimeState& chunk, TERRAIN::TerrainFootprintKind footprintKind, float localX,
                          float localZ, float localRadiusX, float localRadiusZ, float localVerticalRadius,
                          const MATH::Vec2& localTangent, const MATH::Vec2& localBitangent, float edgeSoftness,
                          float sinkDepth, bool holdOnly);
bool RelaxChunk(SAND::SandChunkRuntimeState& chunk, float fixedDt);
bool FootprintBasisUsable(const MATH::Vec2& tangent, const MATH::Vec2& bitangent) noexcept;
bool EvaluateFootprintFactor(TERRAIN::TerrainFootprintKind kind, float dx, float dz, const MATH::Vec2& tangent,
                             const MATH::Vec2& bitangent, float radiusX, float radiusZ, float& outFactor) noexcept;
} // namespace HIKARI::SAND
