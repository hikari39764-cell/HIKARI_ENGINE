#include "Render3D/Runtime/HIKARI_SceneSurfaceFloatPool.h"

#include <DirectXPackedVector.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace HIKARI::RENDER3D::RUNTIME {

namespace {
constexpr float kMaximumEncodedSlope = 1024.0f;

// 勾配は half2 を 1 slot に詰める。shader は f16tof32 で戻す。
float EncodeSlope(float slopeX, float slopeZ) noexcept {
    const auto half = [](float value) {
        const float safe = std::isfinite(value) ? std::clamp(value, -kMaximumEncodedSlope, kMaximumEncodedSlope) : 0.0f;
        return static_cast<uint32_t>(DirectX::PackedVector::XMConvertFloatToHalf(safe));
    };
    return std::bit_cast<float>(half(slopeX) | (half(slopeZ) << 16u));
}

// layer weight は unorm8 x4 を 1 slot に詰める。
float EncodeLayerWeights(const float (&weights)[TERRAIN::kTerrainSurfaceLayersPerWeightPlane]) noexcept {
    uint32_t packed = 0u;
    for (uint32_t channel = 0u; channel < TERRAIN::kTerrainSurfaceLayersPerWeightPlane; ++channel) {
        const float value = std::isfinite(weights[channel]) ? std::clamp(weights[channel], 0.0f, 1.0f) : 0.0f;
        packed |= static_cast<uint32_t>(std::lround(value * 255.0f)) << (8u * channel);
    }
    return std::bit_cast<float>(packed);
}

uint32_t WeightPlaneCount(uint32_t layerCount) noexcept {
    return layerCount > 1u ? (layerCount + TERRAIN::kTerrainSurfaceLayersPerWeightPlane - 1u) /
                                 TERRAIN::kTerrainSurfaceLayersPerWeightPlane
                           : 0u;
}

// target 格子の頂点 (x, z) における source 格子の双線形値。同解像度なら厳密値。
class SquareFieldSampler {
public:
    SquareFieldSampler(uint32_t sourceGrid, uint32_t targetGrid) : sourceGrid_(sourceGrid), targetGrid_(targetGrid) {}

    float Sample(const std::vector<float>& values, std::size_t baseOffset, uint32_t x, uint32_t z) const noexcept {
        const uint32_t sourceStride = sourceGrid_ + 1u;
        if (sourceGrid_ == targetGrid_) {
            return values[baseOffset + static_cast<std::size_t>(z) * sourceStride + x];
        }
        const float scale = static_cast<float>(sourceGrid_) / static_cast<float>(targetGrid_);
        const float sourceX = static_cast<float>(x) * scale;
        const float sourceZ = static_cast<float>(z) * scale;
        const uint32_t x0 = (std::min)(static_cast<uint32_t>(sourceX), sourceGrid_);
        const uint32_t z0 = (std::min)(static_cast<uint32_t>(sourceZ), sourceGrid_);
        const uint32_t x1 = (std::min)(x0 + 1u, sourceGrid_);
        const uint32_t z1 = (std::min)(z0 + 1u, sourceGrid_);
        const float tx = sourceX - static_cast<float>(x0);
        const float tz = sourceZ - static_cast<float>(z0);
        const auto at = [&](uint32_t sx, uint32_t sz) {
            return values[baseOffset + static_cast<std::size_t>(sz) * sourceStride + sx];
        };
        return std::lerp(std::lerp(at(x0, z0), at(x1, z0), tx), std::lerp(at(x0, z1), at(x1, z1), tx), tz);
    }

private:
    uint32_t sourceGrid_ = 1u;
    uint32_t targetGrid_ = 1u;
};

// 1 plane 分の値を target 格子の行 [minZ, maxZ] について書く。
template <typename TWrite, typename TValue>
void WritePlaneRows(uint32_t targetGrid, uint32_t minZ, uint32_t maxZ, TWrite&& write, TValue&& value) {
    const uint32_t stride = targetGrid + 1u;
    for (uint32_t z = minZ; z <= maxZ; ++z) {
        for (uint32_t x = 0u; x < stride; ++x) {
            write(static_cast<std::size_t>(z) * stride + x, value(x, z));
        }
    }
}

struct TerrainPlaneWriter {
    const TERRAIN::TerrainSurfaceProjectedFields& fields;
    const SquareFieldSampler& sampler;
    uint32_t layerCount = 1u;
    std::size_t cellCount = 0u;

    float Height(uint32_t x, uint32_t z) const { return sampler.Sample(fields.heights, 0u, x, z); }
    float Slope(uint32_t x, uint32_t z) const {
        return EncodeSlope(sampler.Sample(fields.slopeX, 0u, x, z), sampler.Sample(fields.slopeZ, 0u, x, z));
    }
    float Coverage(uint32_t x, uint32_t z) const {
        return fields.coverage.empty() ? 1.0f : sampler.Sample(fields.coverage, 0u, x, z);
    }
    float Weights(uint32_t plane, uint32_t x, uint32_t z) const {
        float weights[TERRAIN::kTerrainSurfaceLayersPerWeightPlane]{};
        for (uint32_t channel = 0u; channel < TERRAIN::kTerrainSurfaceLayersPerWeightPlane; ++channel) {
            const uint32_t layer = plane * TERRAIN::kTerrainSurfaceLayersPerWeightPlane + channel;
            if (layer < layerCount) {
                weights[channel] =
                    sampler.Sample(fields.layerWeights, static_cast<std::size_t>(layer) * cellCount, x, z);
            }
        }
        return EncodeLayerWeights(weights);
    }
};

bool HasCompleteCoreFields(const TERRAIN::TerrainSurfaceProjectedFields& fields, std::size_t cellCount,
                           uint32_t layerCount) noexcept {
    const bool layerWeightsComplete =
        layerCount <= 1u || (cellCount <= (std::numeric_limits<std::size_t>::max)() / layerCount &&
                             fields.layerWeights.size() >= cellCount * layerCount);
    return fields.heights.size() >= cellCount && fields.slopeX.size() >= cellCount &&
           fields.slopeZ.size() >= cellCount && (fields.coverage.empty() || fields.coverage.size() >= cellCount) &&
           layerWeightsComplete;
}
} // namespace

bool CollectSceneSurfaceFloatRangesSince(std::span<const SceneSurfaceFloatPoolDelta> history, uint64_t baseVersion,
                                         uint64_t targetVersion, std::vector<SceneSurfaceFloatRange>& outRanges) {
    outRanges.clear();
    if (baseVersion == targetVersion) {
        return true;
    }
    uint64_t cursor = baseVersion;
    for (const SceneSurfaceFloatPoolDelta& delta : history) {
        if (delta.baseVersion != cursor) {
            continue;
        }
        outRanges.insert(outRanges.end(), delta.ranges.begin(), delta.ranges.end());
        cursor = delta.version;
        if (cursor == targetVersion) {
            return true;
        }
    }
    outRanges.clear();
    return false;
}

uint32_t TerrainSurfaceCoreFloatsPerVertex(uint32_t layerCount) noexcept {
    return 3u + WeightPlaneCount((std::max)(layerCount, 1u));
}

std::size_t ResolveSquareCellCount(uint32_t gridResolution) noexcept {
    const std::size_t side = static_cast<std::size_t>(gridResolution) + 1u;
    if (side > (std::numeric_limits<std::size_t>::max)() / side) {
        return 0u;
    }
    return side * side;
}

SceneSurfaceFloatPoolPacker::SceneSurfaceFloatPoolPacker(std::vector<float>& destination, std::size_t capacity)
    : destination_(destination), capacity_(capacity) {
    destination_.clear();
    if (capacity_ >= kSceneSurfaceNeutralFloatCount) {
        destination_.assign(kSceneSurfaceNeutralFloatCount, 0.0f);
    }
}

std::size_t SceneSurfaceFloatPoolPacker::GetRemaining() const noexcept {
    return destination_.size() <= capacity_ ? capacity_ - destination_.size() : 0u;
}

bool SceneSurfaceFloatPoolPacker::IsUsable() const noexcept {
    return destination_.size() >= kSceneSurfaceNeutralFloatCount && destination_.size() <= capacity_;
}

uint32_t SceneSurfaceFloatPoolPacker::CurrentOffset() const noexcept {
    return destination_.size() <= static_cast<std::size_t>((std::numeric_limits<uint32_t>::max)())
               ? static_cast<uint32_t>(destination_.size())
               : 0u;
}

SceneSurfaceTerrainFloatAllocation SceneSurfaceFloatPoolPacker::PackTerrainCore(
    const SceneSurfaceTerrainFloatFields& source) {
    SceneSurfaceTerrainFloatAllocation result{};
    const uint32_t gridResolution = (std::max)(source.gridResolution, 1u);
    const uint32_t layerCount = std::clamp(source.layerCount, 1u, 8u);
    if (!IsUsable() || gridResolution > 512u || source.fields == nullptr || source.layerStyles == nullptr ||
        source.layerStyles->size() < layerCount) {
        return result;
    }
    const std::size_t cellCount = ResolveSquareCellCount(gridResolution);
    if (cellCount == 0u || !HasCompleteCoreFields(*source.fields, cellCount, layerCount)) {
        return result;
    }

    // style は格子に依存しない固定量。残りから収まる最大格子を選ぶ。
    const std::size_t styleFloats = static_cast<std::size_t>(layerCount) * TERRAIN::kTerrainSurfaceLayerStyleFloatCount;
    if (styleFloats >= GetRemaining()) {
        return result;
    }
    const std::size_t availableCells = (GetRemaining() - styleFloats) / TerrainSurfaceCoreFloatsPerVertex(layerCount);
    const double side = std::floor(std::sqrt(static_cast<double>(availableCells)));
    if (side < 2.0) {
        return result;
    }
    const uint32_t packedGrid = (std::min)(gridResolution, static_cast<uint32_t>(side) - 1u);
    const std::size_t packedCellCount = ResolveSquareCellCount(packedGrid);

    const SquareFieldSampler sampler(gridResolution, packedGrid);
    const TerrainPlaneWriter writer{*source.fields, sampler, layerCount, cellCount};
    const auto appendPlane = [&](auto&& value) {
        const uint32_t offset = CurrentOffset();
        destination_.resize(destination_.size() + packedCellCount);
        float* plane = destination_.data() + offset;
        WritePlaneRows(
            packedGrid, 0u, packedGrid, [&](std::size_t index, float encoded) { plane[index] = encoded; }, value);
        return offset;
    };

    result.gridResolution = packedGrid;
    result.layerCount = layerCount;
    result.offsets.height = appendPlane([&](uint32_t x, uint32_t z) { return writer.Height(x, z); });
    result.offsets.slope = appendPlane([&](uint32_t x, uint32_t z) { return writer.Slope(x, z); });
    result.offsets.coverage = appendPlane([&](uint32_t x, uint32_t z) { return writer.Coverage(x, z); });
    for (uint32_t plane = 0u; plane < WeightPlaneCount(layerCount); ++plane) {
        const uint32_t offset = appendPlane([&](uint32_t x, uint32_t z) { return writer.Weights(plane, x, z); });
        if (plane == 0u) {
            result.offsets.layerWeights = offset;
        }
    }
    result.offsets.layerStyles = CurrentOffset();
    for (uint32_t layer = 0u; layer < layerCount; ++layer) {
        const TERRAIN::TerrainSurfaceLayerRenderStyle& style = (*source.layerStyles)[layer];
        destination_.insert(destination_.end(), {
                                                    style.lightTint.x,      style.lightTint.y,
                                                    style.lightTint.z,      style.textureStrength,
                                                    style.darkTint.x,       style.darkTint.y,
                                                    style.darkTint.z,       style.normalStrength,
                                                    style.atlasScaleBias.x, style.atlasScaleBias.y,
                                                    style.atlasScaleBias.z, style.atlasScaleBias.w,
                                                    style.tilesPerMeter,    style.roughness,
                                                    style.saturation,       static_cast<float>(style.materialKind),
                                                    style.shadowStrength,   style.granularStrength,
                                                    style.facetSizeMeters,  style.facetStrength,
                                                });
    }
    result.resident = true;
    return result;
}

bool SceneSurfaceFloatPoolPacker::TryPackSquareField(const std::vector<float>& values, uint32_t gridResolution,
                                                     uint32_t& outOffset) {
    return TryPackSquareFieldResampled(values, gridResolution, gridResolution, outOffset);
}

bool SceneSurfaceFloatPoolPacker::TryPackSquareFieldResampled(const std::vector<float>& values,
                                                              uint32_t sourceGridResolution,
                                                              uint32_t targetGridResolution, uint32_t& outOffset) {
    outOffset = 0u;
    const uint32_t sourceGrid = (std::max)(sourceGridResolution, 1u);
    const uint32_t targetGrid = (std::max)(targetGridResolution, 1u);
    const std::size_t sourceCount = ResolveSquareCellCount(sourceGrid);
    const std::size_t targetCount = ResolveSquareCellCount(targetGrid);
    if (!IsUsable() || sourceGrid > 512u || targetGrid > sourceGrid || sourceCount == 0u || targetCount == 0u ||
        values.size() < sourceCount || targetCount > GetRemaining()) {
        return false;
    }
    outOffset = CurrentOffset();
    const SquareFieldSampler sampler(sourceGrid, targetGrid);
    destination_.resize(destination_.size() + targetCount);
    float* plane = destination_.data() + outOffset;
    WritePlaneRows(
        targetGrid, 0u, targetGrid, [&](std::size_t index, float value) { plane[index] = value; },
        [&](uint32_t x, uint32_t z) { return sampler.Sample(values, 0u, x, z); });
    return true;
}

bool PatchTerrainSurfaceRows(std::vector<float>& pool, const SceneSurfaceTerrainFloatAllocation& allocation,
                             const TERRAIN::TerrainSurfaceProjectedFields& fields, uint32_t minZ, uint32_t maxZ,
                             std::vector<SceneSurfaceFloatRange>& outRanges) {
    const uint32_t grid = allocation.gridResolution;
    const std::size_t cellCount = ResolveSquareCellCount(grid);
    const uint32_t stride = grid + 1u;
    if (!allocation.resident || cellCount == 0u || minZ > maxZ || maxZ > grid ||
        !HasCompleteCoreFields(fields, cellCount, allocation.layerCount)) {
        return false;
    }
    const SquareFieldSampler sampler(grid, grid);
    const TerrainPlaneWriter writer{fields, sampler, allocation.layerCount, cellCount};
    const auto patchPlane = [&](uint32_t offset, auto&& value) {
        if (offset == 0u || static_cast<std::size_t>(offset) + cellCount > pool.size()) {
            return false;
        }
        float* plane = pool.data() + offset;
        WritePlaneRows(grid, minZ, maxZ, [&](std::size_t index, float encoded) { plane[index] = encoded; }, value);
        outRanges.push_back({offset + minZ * stride, (maxZ - minZ + 1u) * stride});
        return true;
    };

    bool patched =
        patchPlane(allocation.offsets.height, [&](uint32_t x, uint32_t z) { return writer.Height(x, z); }) &&
        patchPlane(allocation.offsets.slope, [&](uint32_t x, uint32_t z) { return writer.Slope(x, z); }) &&
        patchPlane(allocation.offsets.coverage, [&](uint32_t x, uint32_t z) { return writer.Coverage(x, z); });
    for (uint32_t plane = 0u; patched && plane < WeightPlaneCount(allocation.layerCount); ++plane) {
        patched = patchPlane(allocation.offsets.layerWeights + plane * static_cast<uint32_t>(cellCount),
                             [&](uint32_t x, uint32_t z) { return writer.Weights(plane, x, z); });
    }
    if (patched && allocation.offsets.deformation != 0u) {
        patched = fields.deformations.size() >= cellCount &&
                  patchPlane(allocation.offsets.deformation,
                             [&](uint32_t x, uint32_t z) { return sampler.Sample(fields.deformations, 0u, x, z); });
    }
    return patched;
}

} // namespace HIKARI::RENDER3D::RUNTIME
