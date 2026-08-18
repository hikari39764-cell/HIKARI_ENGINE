#pragma once

#include <algorithm>
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
        SceneSurfaceFloatPoolPacker(
            std::vector<float>& destination,
            std::size_t capacity)
            : destination_(destination)
            , capacity_(capacity) {

            destination_.clear();
            if (capacity_ >= kSceneSurfaceNeutralFloatCount) {
                destination_.assign(kSceneSurfaceNeutralFloatCount, 0.0f);
            }
        }

        std::size_t GetSize() const noexcept {
            return destination_.size();
        }

        std::size_t GetRemaining() const noexcept {
            return destination_.size() <= capacity_
                ? capacity_ - destination_.size()
                : 0u;
        }

        bool IsUsable() const noexcept {
            return destination_.size() >= kSceneSurfaceNeutralFloatCount &&
                destination_.size() <= capacity_;
        }

        bool Contains(uint32_t offset, std::size_t count) const noexcept {
            const std::size_t start = static_cast<std::size_t>(offset);
            return start <= destination_.size() &&
                count <= destination_.size() - start;
        }

        SceneSurfaceTerrainFloatAllocation PackTerrainCore(
            const SceneSurfaceTerrainFloatFields& fields) {

            SceneSurfaceTerrainFloatAllocation result{};
            const uint32_t gridResolution =
                (std::max)(fields.gridResolution, 1u);
            if (!IsUsable() || gridResolution > 512u ||
                fields.heights == nullptr) {
                return result;
            }

            const std::size_t cellCount =
                ResolveSquareCellCount(gridResolution);
            if (cellCount == 0u || fields.heights->size() < cellCount) {
                return result;
            }

            const bool hasCoverage =
                fields.coverage != nullptr && !fields.coverage->empty();
            if (hasCoverage && fields.coverage->size() < cellCount) {
                return result;
            }

            const uint32_t requestedLayerCount =
                std::clamp(fields.layerCount, 1u, 0x7Fu);
            const bool hasLayerWeights =
                requestedLayerCount > 1u &&
                fields.layerWeights != nullptr &&
                !fields.layerWeights->empty();
            if (hasLayerWeights &&
                (cellCount >
                    (std::numeric_limits<std::size_t>::max)() /
                        requestedLayerCount ||
                 fields.layerWeights->size() <
                    cellCount * requestedLayerCount)) {
                return result;
            }

            const uint32_t packedLayerCount =
                hasLayerWeights ? requestedLayerCount : 1u;
            const std::size_t fieldMultiplier =
                1u +
                (hasCoverage ? 1u : 0u) +
                (hasLayerWeights
                    ? static_cast<std::size_t>(packedLayerCount)
                    : 0u);
            if (cellCount > GetRemaining() / fieldMultiplier) {
                return result;
            }

            result.gridResolution = gridResolution;
            result.layerCount = packedLayerCount;
            result.heightOffset = CurrentOffset();
            AppendExact(*fields.heights, 0u, cellCount);
            if (hasLayerWeights) {
                result.layerWeightsOffset = CurrentOffset();
                AppendExact(
                    *fields.layerWeights,
                    0u,
                    cellCount * packedLayerCount);
            }
            if (hasCoverage) {
                result.coverageOffset = CurrentOffset();
                AppendExact(*fields.coverage, 0u, cellCount);
            }
            result.resident = true;
            return result;
        }

        bool TryPackSquareField(
            const std::vector<float>& values,
            uint32_t gridResolution,
            uint32_t& outOffset) {

            outOffset = 0u;
            const uint32_t safeGrid =
                (std::max)(gridResolution, 1u);
            const std::size_t cellCount =
                ResolveSquareCellCount(safeGrid);
            if (!IsUsable() || safeGrid > 512u || cellCount == 0u ||
                values.size() < cellCount || cellCount > GetRemaining()) {
                return false;
            }
            outOffset = CurrentOffset();
            AppendExact(values, 0u, cellCount);
            return true;
        }

        bool TryPackLinear(
            std::span<const float> values,
            uint32_t& outOffset) {

            outOffset = 0u;
            if (!IsUsable() || values.empty() ||
                values.size() > GetRemaining()) {
                return false;
            }
            outOffset = CurrentOffset();
            destination_.insert(
                destination_.end(),
                values.begin(),
                values.end());
            return true;
        }

        static std::size_t ResolveSquareCellCount(
            uint32_t gridResolution) noexcept {

            const std::size_t side =
                static_cast<std::size_t>(gridResolution) + 1u;
            if (side >
                (std::numeric_limits<std::size_t>::max)() / side) {
                return 0u;
            }
            return side * side;
        }

    private:
        uint32_t CurrentOffset() const noexcept {
            return destination_.size() <=
                static_cast<std::size_t>((std::numeric_limits<uint32_t>::max)())
                ? static_cast<uint32_t>(destination_.size())
                : 0u;
        }

        void AppendExact(
            const std::vector<float>& values,
            std::size_t sourceOffset,
            std::size_t count) {

            destination_.insert(
                destination_.end(),
                values.begin() + sourceOffset,
                values.begin() + sourceOffset + count);
        }

        std::vector<float>& destination_;
        std::size_t capacity_ = 0u;
    };

} // namespace HIKARI::RENDER3D::RUNTIME
