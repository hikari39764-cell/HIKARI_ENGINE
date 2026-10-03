#pragma once

#include <cstdint>
#include <vector>

#include "Render3D/Terrain/HIKARI_TerrainSurfaceRenderData.h"

namespace HIKARI::TERRAIN::RENDER {

// 頂点の最終高さと勾配を決める入力。field 以外の値が変われば全頂点を焼き直す。
struct TerrainSurfaceBakeInputs {
    uint32_t gridResolution = 1u;
    MATH::Vec2 extents{1.0f, 1.0f};
    float rippleAmount = 0.0f;
    float heightScale = 0.0f;
    // 縁の沈降深さ。edgeDepth が 0 の時は rimStrength を使う。
    float edgeSinkDepth = 0.0f;
    // layer 0 の低ポリ面。
    float facetSizeMeters = 1.6f;
    float facetStrength = 0.0f;
    std::vector<float> granularStrength{};

    bool operator==(const TerrainSurfaceBakeInputs& rhs) const noexcept;
};

TerrainSurfaceBakeInputs ResolveTerrainSurfaceBakeInputs(const RENDER3D::TERRAIN::TerrainSurfaceRenderData& data);

// rect 内の field が変わった時に焼き直しが必要な外周幅 (格子単位)。
uint32_t ResolveTerrainSurfaceBakeMargin(const TerrainSurfaceBakeInputs& inputs) noexcept;

// sourceHeights (runtime 応答込みの地形高さ) と coverage / layer weight から、
// rect 内の頂点の最終高さ・勾配を fields へ書く。ripple・縁の沈降・低ポリ面を含む。
void BakeTerrainSurfaceRect(const TerrainSurfaceBakeInputs& inputs, const std::vector<float>& sourceHeights,
                            const RENDER3D::TERRAIN::TerrainSurfaceFieldRect& rect,
                            RENDER3D::TERRAIN::TerrainSurfaceProjectedFields& fields);

} // namespace HIKARI::TERRAIN::RENDER
