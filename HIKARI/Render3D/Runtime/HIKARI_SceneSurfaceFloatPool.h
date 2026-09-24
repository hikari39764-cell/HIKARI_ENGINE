#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace HIKARI::RENDER3D::RUNTIME {

// gSandSurfaceHeights は全 sand field と water coverage が共有する。
// offset=0/gridResolution=1 を安全な可視 fallback にするため 4 zero を予約する。
constexpr std::size_t kSceneSurfaceNeutralFloatCount = 4u;

struct SceneSurfaceTerrainFloatFields {
    uint32_t gridResolution = 1u;
    uint32_t layerCount = 1u;
    const std::vector<float>* heights = nullptr;
    const std::vector<float>* layerWeights = nullptr;
    const std::vector<float>* coverage = nullptr;
};

struct SceneSurfaceTerrainFloatAllocation {
    bool resident = false;
    uint32_t gridResolution = 1u;
    uint32_t layerCount = 1u;
    uint32_t heightOffset = 0u;
    uint32_t layerWeightsOffset = 0u;
    uint32_t coverageOffset = 0u;
};

// Scene surface float pool 唯一の deterministic first-fit owner。
// terrain core は atomic に収容し、optional field は offset zero へ縮退する。
class SceneSurfaceFloatPoolPacker final {
public:
    SceneSurfaceFloatPoolPacker(std::vector<float>& destination, std::size_t capacity)
        : destination_(destination), capacity_(capacity) {
        destination_.clear();
        if (capacity_ >= kSceneSurfaceNeutralFloatCount) {
            destination_.assign(kSceneSurfaceNeutralFloatCount, 0.0f);
        }
    }

    std::size_t GetSize() const noexcept { return destination_.size(); }

    std::size_t GetRemaining() const noexcept {
        return destination_.size() <= capacity_ ? capacity_ - destination_.size() : 0u;
    }

    bool IsUsable() const noexcept {
        return destination_.size() >= kSceneSurfaceNeutralFloatCount && destination_.size() <= capacity_;
    }

    bool Contains(uint32_t offset, std::size_t count) const noexcept {
        const std::size_t start = static_cast<std::size_t>(offset);
        return start <= destination_.size() && count <= destination_.size() - start;
    }

    SceneSurfaceTerrainFloatAllocation PackTerrainCore(const SceneSurfaceTerrainFloatFields& fields) {
        SceneSurfaceTerrainFloatAllocation result{};
        const uint32_t gridResolution = (std::max)(fields.gridResolution, 1u);
        if (!IsUsable() || gridResolution > 512u || fields.heights == nullptr) {
            return result;
        }

        const std::size_t cellCount = ResolveSquareCellCount(gridResolution);
        if (cellCount == 0u || fields.heights->size() < cellCount) {
            return result;
        }

        const bool hasCoverage = fields.coverage != nullptr && !fields.coverage->empty();
        if (hasCoverage && fields.coverage->size() < cellCount) {
            return result;
        }

        const uint32_t requestedLayerCount = std::clamp(fields.layerCount, 1u, 0x7Fu);
        const bool hasLayerWeights =
            requestedLayerCount > 1u && fields.layerWeights != nullptr && !fields.layerWeights->empty();
        if (hasLayerWeights && (cellCount > (std::numeric_limits<std::size_t>::max)() / requestedLayerCount ||
                                fields.layerWeights->size() < cellCount * requestedLayerCount)) {
            return result;
        }

        const uint32_t packedLayerCount = hasLayerWeights ? requestedLayerCount : 1u;
        const std::size_t fieldMultiplier =
            1u + (hasCoverage ? 1u : 0u) + (hasLayerWeights ? static_cast<std::size_t>(packedLayerCount) : 0u);
        const uint32_t packedGridResolution = ResolveLargestResidentGrid(gridResolution, fieldMultiplier);
        if (packedGridResolution == 0u) {
            return result;
        }
        result.gridResolution = packedGridResolution;
        result.layerCount = packedLayerCount;
        result.heightOffset = CurrentOffset();
        AppendSquareField(*fields.heights, gridResolution, packedGridResolution, 0u);
        if (hasLayerWeights) {
            result.layerWeightsOffset = CurrentOffset();
            for (uint32_t layer = 0u; layer < packedLayerCount; ++layer) {
                AppendSquareField(*fields.layerWeights, gridResolution, packedGridResolution,
                                  static_cast<std::size_t>(layer) * cellCount);
            }
        }
        if (hasCoverage) {
            result.coverageOffset = CurrentOffset();
            AppendSquareField(*fields.coverage, gridResolution, packedGridResolution, 0u);
        }
        result.resident = true;
        return result;
    }

    bool TryPackSquareField(const std::vector<float>& values, uint32_t gridResolution, uint32_t& outOffset) {
        outOffset = 0u;
        const uint32_t safeGrid = (std::max)(gridResolution, 1u);
        const std::size_t cellCount = ResolveSquareCellCount(safeGrid);
        if (!IsUsable() || safeGrid > 512u || cellCount == 0u || values.size() < cellCount ||
            cellCount > GetRemaining()) {
            return false;
        }
        outOffset = CurrentOffset();
        AppendExact(values, 0u, cellCount);
        return true;
    }

    bool TryPackSquareFieldResampled(const std::vector<float>& values, uint32_t sourceGridResolution,
                                     uint32_t targetGridResolution, uint32_t& outOffset) {
        outOffset = 0u;
        const uint32_t safeSourceGrid = (std::max)(sourceGridResolution, 1u);
        const uint32_t safeTargetGrid = (std::max)(targetGridResolution, 1u);
        const std::size_t sourceCount = ResolveSquareCellCount(safeSourceGrid);
        const std::size_t targetCount = ResolveSquareCellCount(safeTargetGrid);
        if (!IsUsable() || safeSourceGrid > 512u || safeTargetGrid > safeSourceGrid || sourceCount == 0u ||
            targetCount == 0u || values.size() < sourceCount || targetCount > GetRemaining()) {
            return false;
        }
        outOffset = CurrentOffset();
        AppendSquareField(values, safeSourceGrid, safeTargetGrid, 0u);
        return true;
    }

    bool TryPackLinear(std::span<const float> values, uint32_t& outOffset) {
        outOffset = 0u;
        if (!IsUsable() || values.empty() || values.size() > GetRemaining()) {
            return false;
        }
        outOffset = CurrentOffset();
        destination_.insert(destination_.end(), values.begin(), values.end());
        return true;
    }

    static std::size_t ResolveSquareCellCount(uint32_t gridResolution) noexcept {
        const std::size_t side = static_cast<std::size_t>(gridResolution) + 1u;
        if (side > (std::numeric_limits<std::size_t>::max)() / side) {
            return 0u;
        }
        return side * side;
    }

private:
    uint32_t ResolveLargestResidentGrid(uint32_t requestedGridResolution, std::size_t fieldMultiplier) const noexcept {
        if (fieldMultiplier == 0u) {
            return 0u;
        }
        const std::size_t availableCells = GetRemaining() / fieldMultiplier;
        if (availableCells < 4u) {
            return 0u;
        }
        const double side = std::floor(std::sqrt(static_cast<double>(availableCells)));
        if (side < 2.0) {
            return 0u;
        }
        const uint32_t budgetGrid = static_cast<uint32_t>(side) - 1u;
        return (std::min)(requestedGridResolution, budgetGrid);
    }

    uint32_t CurrentOffset() const noexcept {
        return destination_.size() <= static_cast<std::size_t>((std::numeric_limits<uint32_t>::max)())
                   ? static_cast<uint32_t>(destination_.size())
                   : 0u;
    }

    void AppendExact(const std::vector<float>& values, std::size_t sourceOffset, std::size_t count) {
        destination_.insert(destination_.end(), values.begin() + sourceOffset, values.begin() + sourceOffset + count);
    }

    void AppendSquareField(const std::vector<float>& values, uint32_t sourceGridResolution,
                           uint32_t targetGridResolution, std::size_t sourceOffset) {
        if (sourceGridResolution == targetGridResolution) {
            AppendExact(values, sourceOffset, ResolveSquareCellCount(sourceGridResolution));
            return;
        }

        const uint32_t sourceStride = sourceGridResolution + 1u;
        const uint32_t targetStride = targetGridResolution + 1u;
        destination_.reserve(destination_.size() + ResolveSquareCellCount(targetGridResolution));
        for (uint32_t z = 0u; z < targetStride; ++z) {
            const float sourceZ = static_cast<float>(z) * static_cast<float>(sourceGridResolution) /
                                  static_cast<float>(targetGridResolution);
            const uint32_t z0 = static_cast<uint32_t>(sourceZ);
            const uint32_t z1 = (std::min)(z0 + 1u, sourceGridResolution);
            const float tz = sourceZ - static_cast<float>(z0);
            for (uint32_t x = 0u; x < targetStride; ++x) {
                const float sourceX = static_cast<float>(x) * static_cast<float>(sourceGridResolution) /
                                      static_cast<float>(targetGridResolution);
                const uint32_t x0 = static_cast<uint32_t>(sourceX);
                const uint32_t x1 = (std::min)(x0 + 1u, sourceGridResolution);
                const float tx = sourceX - static_cast<float>(x0);
                const auto sample = [&](uint32_t sx, uint32_t sz) {
                    return values[sourceOffset + static_cast<std::size_t>(sz) * sourceStride + sx];
                };
                const float top = std::lerp(sample(x0, z0), sample(x1, z0), tx);
                const float bottom = std::lerp(sample(x0, z1), sample(x1, z1), tx);
                destination_.push_back(std::lerp(top, bottom, tz));
            }
        }
    }

    std::vector<float>& destination_;
    std::size_t capacity_ = 0u;
};

} // namespace HIKARI::RENDER3D::RUNTIME
