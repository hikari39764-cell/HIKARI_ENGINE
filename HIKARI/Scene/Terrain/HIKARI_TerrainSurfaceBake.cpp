#include "Scene/Terrain/HIKARI_TerrainSurfaceBake.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::TERRAIN::RENDER {

namespace {
float FiniteOr(float value, float fallback) noexcept { return std::isfinite(value) ? value : fallback; }

float Saturate(float value) noexcept { return std::clamp(value, 0.0f, 1.0f); }

float SmoothEdge(float edge) noexcept { return edge * edge * (3.0f - 2.0f * edge); }

// 格子上の双線形サンプル。uv は [0,1] へ丸め、端は外側の頂点を繰り返す。
class TerrainGrid {
public:
    explicit TerrainGrid(const TerrainSurfaceBakeInputs& inputs) : grid_(inputs.gridResolution), stride_(grid_ + 1u) {}

    std::size_t Index(uint32_t x, uint32_t z) const noexcept { return static_cast<std::size_t>(z) * stride_ + x; }

    uint32_t Clamp(int64_t value) const noexcept {
        return static_cast<uint32_t>(std::clamp<int64_t>(value, 0, static_cast<int64_t>(grid_)));
    }

    float Sample(const std::vector<float>& values, float u, float v) const noexcept {
        const float fu = Saturate(u) * static_cast<float>(grid_);
        const float fv = Saturate(v) * static_cast<float>(grid_);
        const uint32_t x0 = (std::min)(static_cast<uint32_t>(fu), grid_);
        const uint32_t z0 = (std::min)(static_cast<uint32_t>(fv), grid_);
        const uint32_t x1 = (std::min)(x0 + 1u, grid_);
        const uint32_t z1 = (std::min)(z0 + 1u, grid_);
        const float tx = fu - static_cast<float>(x0);
        const float tz = fv - static_cast<float>(z0);
        const float top = std::lerp(values[Index(x0, z0)], values[Index(x1, z0)], tx);
        const float bottom = std::lerp(values[Index(x0, z1)], values[Index(x1, z1)], tx);
        return std::lerp(top, bottom, tz);
    }

private:
    uint32_t grid_ = 1u;
    uint32_t stride_ = 2u;
};

float EvaluateRippleHeight(float localX, float localZ, float rippleAmount, float heightScale) noexcept {
    const float ripple = std::clamp(rippleAmount, 0.0f, 3.0f);
    const float scale = std::clamp(heightScale, 0.0f, 2.0f);
    const float wave = std::sin(localX * 0.85f + 1.7f * std::sin(localZ * 0.21f)) * 0.012f * ripple +
                       std::sin(localZ * 0.63f - 0.9f * std::sin(localX * 0.17f)) * 0.009f * ripple;
    return wave * scale;
}

float ResolveGranularWeight(const TerrainSurfaceBakeInputs& inputs,
                            const RENDER3D::TERRAIN::TerrainSurfaceProjectedFields& fields, std::size_t vertex,
                            std::size_t cellCount) noexcept {
    const std::size_t layerCount = inputs.granularStrength.size();
    if (layerCount <= 1u || fields.layerWeights.size() < cellCount * layerCount) {
        return layerCount != 0u ? Saturate(inputs.granularStrength[0]) : 1.0f;
    }
    float result = 0.0f;
    float weightSum = 0.0f;
    for (std::size_t layer = 0u; layer < layerCount; ++layer) {
        const float weight = Saturate(fields.layerWeights[layer * cellCount + vertex]);
        result += weight * Saturate(inputs.granularStrength[layer]);
        weightSum += weight;
    }
    return weightSum > 1.0e-5f ? Saturate(result / weightSum) : 1.0f;
}

// 高密度格子の微小三角を平らにせず、metre 尺度の仮想 coarse cell を 2 枚の
// 三角平面で近似する。checkerboard の対角で一方向の筋を避ける。
void ResolveFacetSlope(const TerrainSurfaceBakeInputs& inputs, const TerrainGrid& grid,
                       const std::vector<float>& heights, float localX, float localZ, float& slopeX, float& slopeZ) {
    const float strength = Saturate(inputs.facetStrength);
    if (strength <= 1.0e-5f) {
        return;
    }
    const float size = (std::max)(inputs.facetSizeMeters, 0.25f);
    const float halfX = inputs.extents.x * 0.5f;
    const float halfZ = inputs.extents.y * 0.5f;
    const float cellIndexX = std::floor((std::max)(localX + halfX, 0.0f) / size);
    const float cellIndexZ = std::floor((std::max)(localZ + halfZ, 0.0f) / size);
    const float minX = std::clamp(cellIndexX * size - halfX, -halfX, halfX);
    const float minZ = std::clamp(cellIndexZ * size - halfZ, -halfZ, halfZ);
    const float maxX = std::clamp(minX + size, -halfX, halfX);
    const float maxZ = std::clamp(minZ + size, -halfZ, halfZ);
    const float extentX = (std::max)(maxX - minX, 1.0e-4f);
    const float extentZ = (std::max)(maxZ - minZ, 1.0e-4f);
    const float cellU = Saturate((localX - minX) / extentX);
    const float cellV = Saturate((localZ - minZ) / extentZ);
    const auto heightAt = [&](float x, float z) {
        return grid.Sample(heights, x / inputs.extents.x + 0.5f, z / inputs.extents.y + 0.5f);
    };
    const float h00 = heightAt(minX, minZ);
    const float h10 = heightAt(maxX, minZ);
    const float h01 = heightAt(minX, maxZ);
    const float h11 = heightAt(maxX, maxZ);

    float facetX = 0.0f;
    float facetZ = 0.0f;
    const uint32_t checker = (static_cast<uint32_t>(cellIndexX) + static_cast<uint32_t>(cellIndexZ)) & 1u;
    if (checker == 0u) {
        const bool lower = cellU + cellV <= 1.0f;
        facetX = lower ? (h10 - h00) / extentX : (h11 - h01) / extentX;
        facetZ = lower ? (h01 - h00) / extentZ : (h11 - h10) / extentZ;
    } else {
        const bool upper = cellU <= cellV;
        facetX = upper ? (h11 - h01) / extentX : (h10 - h00) / extentX;
        facetZ = upper ? (h01 - h00) / extentZ : (h11 - h10) / extentZ;
    }
    slopeX = std::lerp(slopeX, facetX, strength);
    slopeZ = std::lerp(slopeZ, facetZ, strength);
}
} // namespace

bool TerrainSurfaceBakeInputs::operator==(const TerrainSurfaceBakeInputs& rhs) const noexcept {
    return gridResolution == rhs.gridResolution && extents.x == rhs.extents.x && extents.y == rhs.extents.y &&
           rippleAmount == rhs.rippleAmount && heightScale == rhs.heightScale && edgeSinkDepth == rhs.edgeSinkDepth &&
           facetSizeMeters == rhs.facetSizeMeters && facetStrength == rhs.facetStrength &&
           granularStrength == rhs.granularStrength;
}

TerrainSurfaceBakeInputs ResolveTerrainSurfaceBakeInputs(const RENDER3D::TERRAIN::TerrainSurfaceRenderData& data) {
    TerrainSurfaceBakeInputs inputs{};
    inputs.gridResolution = std::clamp(data.gridResolution, 1u, 512u);
    inputs.extents = {(std::max)(FiniteOr(data.extents.x, 20.0f), 0.001f),
                      (std::max)(FiniteOr(data.extents.y, 20.0f), 0.001f)};
    inputs.rippleAmount = std::clamp(FiniteOr(data.rippleAmount, 0.0f), 0.0f, 3.0f);
    inputs.heightScale = std::clamp(FiniteOr(data.heightScale, 0.0f), 0.0f, 2.0f);
    const float edgeDepth = std::clamp(FiniteOr(data.edgeDepth, 0.0f), 0.0f, 100.0f);
    const float rimStrength = std::clamp(FiniteOr(data.rimStrength, 0.0f), 0.0f, 2.0f);
    inputs.edgeSinkDepth = edgeDepth > 1.0e-4f ? edgeDepth : rimStrength;
    if (!data.layerStyles.empty()) {
        inputs.facetSizeMeters = FiniteOr(data.layerStyles.front().facetSizeMeters, 1.6f);
        inputs.facetStrength = FiniteOr(data.layerStyles.front().facetStrength, 0.0f);
    }
    const std::size_t layerCount =
        (std::min)(static_cast<std::size_t>((std::max)(data.layerCount, 1u)), data.layerStyles.size());
    inputs.granularStrength.reserve(layerCount);
    for (std::size_t layer = 0u; layer < layerCount; ++layer) {
        inputs.granularStrength.push_back(FiniteOr(data.layerStyles[layer].granularStrength, 0.0f));
    }
    return inputs;
}

uint32_t ResolveTerrainSurfaceBakeMargin(const TerrainSurfaceBakeInputs& inputs) noexcept {
    if (Saturate(inputs.facetStrength) <= 1.0e-5f) {
        return 1u;
    }
    const float cell = (std::min)(inputs.extents.x, inputs.extents.y) / static_cast<float>(inputs.gridResolution);
    const float cells = std::ceil((std::max)(inputs.facetSizeMeters, 0.25f) / (std::max)(cell, 1.0e-4f));
    return 1u + static_cast<uint32_t>((std::min)(cells, static_cast<float>(inputs.gridResolution)));
}

void BakeTerrainSurfaceRect(const TerrainSurfaceBakeInputs& inputs, const std::vector<float>& sourceHeights,
                            const RENDER3D::TERRAIN::TerrainSurfaceFieldRect& rect,
                            RENDER3D::TERRAIN::TerrainSurfaceProjectedFields& fields) {
    const uint32_t gridResolution = inputs.gridResolution;
    const std::size_t stride = static_cast<std::size_t>(gridResolution) + 1u;
    const std::size_t cellCount = stride * stride;
    if (sourceHeights.size() < cellCount || fields.coverage.size() < cellCount) {
        return;
    }
    fields.heights.resize(cellCount, 0.0f);
    fields.slopeX.resize(cellCount, 0.0f);
    fields.slopeZ.resize(cellCount, 0.0f);

    const TerrainGrid grid(inputs);
    const float cellX = inputs.extents.x / static_cast<float>(gridResolution);
    const float cellZ = inputs.extents.y / static_cast<float>(gridResolution);
    const float depth = inputs.edgeSinkDepth;
    const auto edgeAt = [&](uint32_t x, uint32_t z) { return SmoothEdge(Saturate(fields.coverage[grid.Index(x, z)])); };
    const uint32_t maxX = (std::min)(rect.maxX, gridResolution);
    const uint32_t maxZ = (std::min)(rect.maxZ, gridResolution);
    for (uint32_t z = rect.minZ; z <= maxZ; ++z) {
        const uint32_t zDown = grid.Clamp(static_cast<int64_t>(z) - 1);
        const uint32_t zUp = grid.Clamp(static_cast<int64_t>(z) + 1);
        const float localZ = -inputs.extents.y * 0.5f + static_cast<float>(z) * cellZ;
        for (uint32_t x = rect.minX; x <= maxX; ++x) {
            const uint32_t xLeft = grid.Clamp(static_cast<int64_t>(x) - 1);
            const uint32_t xRight = grid.Clamp(static_cast<int64_t>(x) + 1);
            const float localX = -inputs.extents.x * 0.5f + static_cast<float>(x) * cellX;
            const std::size_t vertex = grid.Index(x, z);

            float slopeX =
                (sourceHeights[grid.Index(xRight, z)] - sourceHeights[grid.Index(xLeft, z)]) / (2.0f * cellX);
            float slopeZ = (sourceHeights[grid.Index(x, zUp)] - sourceHeights[grid.Index(x, zDown)]) / (2.0f * cellZ);
            ResolveFacetSlope(inputs, grid, sourceHeights, localX, localZ, slopeX, slopeZ);
            // coverage の勾配をそのまま縁の沈降へ写す。
            slopeX += depth * (edgeAt(xRight, z) - edgeAt(xLeft, z)) / (2.0f * cellX);
            slopeZ += depth * (edgeAt(x, zUp) - edgeAt(x, zDown)) / (2.0f * cellZ);

            const float granular = ResolveGranularWeight(inputs, fields, vertex, cellCount);
            const float ripple =
                EvaluateRippleHeight(localX, localZ, inputs.rippleAmount * granular, inputs.heightScale);
            const float edgeSink = -depth * (1.0f - edgeAt(x, z));
            fields.heights[vertex] = sourceHeights[vertex] + ripple + edgeSink;
            fields.slopeX[vertex] = slopeX;
            fields.slopeZ[vertex] = slopeZ;
        }
    }
}

} // namespace HIKARI::TERRAIN::RENDER
