#pragma once

#include <cstddef>
#include <cstdint>

#include "Render3D/Sand/HIKARI_SandSurfaceRenderData.h"

namespace HIKARI::RENDER3D::SAND {

    constexpr uint32_t kSandSurfaceGpuSchemaVersion = 2u;
    constexpr uint32_t kSandSurfaceGpuLayerCountShift = 1u;
    constexpr uint32_t kSandSurfaceGpuLayerCountMask = 0x7Fu <<
        kSandSurfaceGpuLayerCountShift;
    constexpr uint32_t kSandSurfaceGpuDeformationOffsetShift = 8u;
    constexpr uint32_t kSandSurfaceGpuMaxDeformationOffset = 0x00FFFFFFu;
    constexpr uint32_t kSandSurfaceGpuDeformationOffsetMask =
        kSandSurfaceGpuMaxDeformationOffset <<
        kSandSurfaceGpuDeformationOffsetShift;

    enum class SandSurfaceGpuFlags : uint32_t {
        None = 0,
        Enabled = 1u << 0,
    };

    // HLSL の HikariSandSurfaceGpuData と同一の sparse record layout。
    // index 0 は全 feature buffer で必須の neutral sentinel とする。
    struct alignas(16) SandSurfaceGpuData {
        uint32_t schemaVersion = 0u;
        uint32_t flags = 0u;
        uint32_t gridResolution = 0u;
        // 高さ配列内の tile offset。sentinel では有効 record 数になる。
        uint32_t heightOffset = 0u;

        MATH::Vec4 extentsScale{ 0.0f, 0.0f, 0.0f, 0.0f };
        MATH::Vec4 styleLight{ 0.0f, 0.0f, 0.0f, 0.0f };
        MATH::Vec4 styleDark{ 0.0f, 0.0f, 0.0f, 0.0f };
        MATH::Vec4 stylePit{ 0.0f, 0.0f, 0.0f, 0.0f };
        MATH::Vec4 styleWet{ 0.0f, 0.0f, 0.0f, 0.0f };
    };

    static_assert(sizeof(SandSurfaceGpuData) == 96u);
    static_assert(alignof(SandSurfaceGpuData) == 16u);
    static_assert(offsetof(SandSurfaceGpuData, schemaVersion) == 0u);
    static_assert(offsetof(SandSurfaceGpuData, heightOffset) == 12u);
    static_assert(offsetof(SandSurfaceGpuData, extentsScale) == 16u);
    static_assert(offsetof(SandSurfaceGpuData, styleLight) == 32u);
    static_assert(offsetof(SandSurfaceGpuData, styleDark) == 48u);
    static_assert(offsetof(SandSurfaceGpuData, stylePit) == 64u);
    static_assert(offsetof(SandSurfaceGpuData, styleWet) == 80u);
    static_assert(
        (static_cast<uint32_t>(SandSurfaceGpuFlags::Enabled) &
            kSandSurfaceGpuLayerCountMask) == 0u);
    static_assert(
        (kSandSurfaceGpuLayerCountMask &
            kSandSurfaceGpuDeformationOffsetMask) == 0u);
    static_assert(
        kSandSurfaceMaxHeightCells - 1u <=
            kSandSurfaceGpuMaxDeformationOffset);

    SandSurfaceGpuData BuildSandSurfaceGpuDataSentinel() noexcept;
    SandSurfaceGpuData BuildSandSurfaceGpuData(
        const SandSurfaceRenderData& source,
        uint32_t heightOffset,
        uint32_t deformationOffset,
        uint32_t layerWeightsOffset,
        uint32_t coverageOffset,
        uint32_t layerTintsOffset = 0u) noexcept;

} // namespace HIKARI::RENDER3D::SAND
