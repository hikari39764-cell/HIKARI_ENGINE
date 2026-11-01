#include "Render3D/Terrain/HIKARI_TerrainSurfaceRenderData.h"
#include "Core/Math/HIKARI_MathValidation.h"

#include "Render3D/Terrain/HIKARI_TerrainSurfaceGpuData.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::RENDER3D::TERRAIN {

namespace {
bool LayerStylesEqual(const std::vector<TerrainSurfaceLayerRenderStyle>& lhs,
                      const std::vector<TerrainSurfaceLayerRenderStyle>& rhs) noexcept {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0u; index < lhs.size(); ++index) {
        const TerrainSurfaceLayerRenderStyle& a = lhs[index];
        const TerrainSurfaceLayerRenderStyle& b = rhs[index];
        if (a.lightTint.x != b.lightTint.x || a.lightTint.y != b.lightTint.y || a.lightTint.z != b.lightTint.z ||
            a.textureStrength != b.textureStrength || a.darkTint.x != b.darkTint.x || a.darkTint.y != b.darkTint.y ||
            a.darkTint.z != b.darkTint.z || a.normalStrength != b.normalStrength ||
            a.atlasScaleBias.x != b.atlasScaleBias.x || a.atlasScaleBias.y != b.atlasScaleBias.y ||
            a.atlasScaleBias.z != b.atlasScaleBias.z || a.atlasScaleBias.w != b.atlasScaleBias.w ||
            a.tilesPerMeter != b.tilesPerMeter || a.roughness != b.roughness || a.saturation != b.saturation ||
            a.materialKind != b.materialKind || a.shadowStrength != b.shadowStrength ||
            a.granularStrength != b.granularStrength || a.facetSizeMeters != b.facetSizeMeters ||
            a.facetStrength != b.facetStrength) {
            return false;
        }
    }
    return true;
}
} // namespace

bool TerrainSurfaceRenderDataEqual(const TerrainSurfaceRenderData& lhs, const TerrainSurfaceRenderData& rhs) noexcept {
    return lhs.gridResolution == rhs.gridResolution && lhs.extents.x == rhs.extents.x &&
           lhs.extents.y == rhs.extents.y && lhs.heightScale == rhs.heightScale && lhs.rimStrength == rhs.rimStrength &&
           lhs.lightTint.x == rhs.lightTint.x && lhs.lightTint.y == rhs.lightTint.y &&
           lhs.lightTint.z == rhs.lightTint.z && lhs.sparkleStrength == rhs.sparkleStrength &&
           lhs.darkTint.x == rhs.darkTint.x && lhs.darkTint.y == rhs.darkTint.y && lhs.darkTint.z == rhs.darkTint.z &&
           lhs.rippleAmount == rhs.rippleAmount && lhs.pitDarkening == rhs.pitDarkening &&
           lhs.toonBandCount == rhs.toonBandCount && lhs.toonBandHardness == rhs.toonBandHardness &&
           lhs.wetness == rhs.wetness && lhs.edgeDepth == rhs.edgeDepth && lhs.boundsMinY == rhs.boundsMinY &&
           lhs.boundsMaxY == rhs.boundsMaxY && lhs.runtimeResponse == rhs.runtimeResponse &&
           lhs.layerCount == rhs.layerCount && LayerStylesEqual(lhs.layerStyles, rhs.layerStyles);
}

TerrainSurfaceGpuData BuildTerrainSurfaceGpuDataSentinel(uint32_t recordCount) noexcept {
    TerrainSurfaceGpuData result{};
    result.schemaVersion = kTerrainSurfaceGpuSchemaVersion;
    result.flags = 0u;
    result.layerCount = recordCount;
    return result;
}

TerrainSurfaceGpuData BuildTerrainSurfaceGpuData(const TerrainSurfaceRenderData& source,
                                                 const TerrainSurfaceFieldOffsets& offsets) noexcept {
    TerrainSurfaceGpuData result{};
    result.schemaVersion = kTerrainSurfaceGpuSchemaVersion;
    result.flags = static_cast<uint32_t>(TerrainSurfaceGpuFlags::Enabled);
    result.gridResolution = std::clamp(source.gridResolution, 1u, 512u);
    result.layerCount = std::clamp(source.layerCount, 1u, 8u);
    result.heightOffset = offsets.height;
    result.slopeOffset = offsets.slope;
    result.coverageOffset = offsets.coverage;
    result.deformationOffset = offsets.deformation;
    result.layerWeightsOffset = offsets.layerWeights;
    result.layerStylesOffset = offsets.layerStyles;
    result.extentsScale = {(std::max)(MATH::FiniteOr(source.extents.x, 20.0f), 0.001f),
                           (std::max)(MATH::FiniteOr(source.extents.y, 20.0f), 0.001f),
                           std::clamp(MATH::FiniteOr(source.heightScale, 0.32f), 0.0f, 2.0f),
                           std::clamp(MATH::FiniteOr(source.rimStrength, 0.28f), 0.0f, 2.0f)};
    result.styleLight = {std::clamp(MATH::FiniteOr(source.lightTint.x, 1.0f), 0.0f, 1.0f),
                         std::clamp(MATH::FiniteOr(source.lightTint.y, 1.0f), 0.0f, 1.0f),
                         std::clamp(MATH::FiniteOr(source.lightTint.z, 1.0f), 0.0f, 1.0f),
                         std::clamp(MATH::FiniteOr(source.sparkleStrength, 0.35f), 0.0f, 2.0f)};
    result.styleDark = {std::clamp(MATH::FiniteOr(source.darkTint.x, 0.0f), 0.0f, 1.0f),
                        std::clamp(MATH::FiniteOr(source.darkTint.y, 0.0f), 0.0f, 1.0f),
                        std::clamp(MATH::FiniteOr(source.darkTint.z, 0.0f), 0.0f, 1.0f),
                        std::clamp(MATH::FiniteOr(source.rippleAmount, 0.45f), 0.0f, 3.0f)};
    result.styleShade = {std::clamp(MATH::FiniteOr(source.pitDarkening, 0.32f), 0.0f, 1.0f),
                         std::clamp(MATH::FiniteOr(source.toonBandHardness, 0.14f), 0.0f, 0.5f),
                         static_cast<float>(std::clamp(source.toonBandCount, 2u, 4u)),
                         std::clamp(MATH::FiniteOr(source.wetness, 0.0f), 0.0f, 1.0f)};
    // 包絡が未確定 (min > max) なら AS は剔除せず可視側へ倒す。
    const float minY = MATH::FiniteOr(source.boundsMinY, 1.0f);
    const float maxY = MATH::FiniteOr(source.boundsMaxY, 0.0f);
    result.envelope = {minY, maxY, std::clamp(MATH::FiniteOr(source.edgeDepth, 1.5f), 0.0f, 100.0f), 0.0f};
    return result;
}

} // namespace HIKARI::RENDER3D::TERRAIN
