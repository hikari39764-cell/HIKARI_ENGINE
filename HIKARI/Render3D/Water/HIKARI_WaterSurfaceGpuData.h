#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Water/HIKARI_WaterSurfaceRenderData.h"

namespace HIKARI::RENDER3D::WATER {

    constexpr uint32_t kWaterSurfaceGpuSchemaVersion = 2u;

    enum class WaterSurfaceGpuFlags : uint32_t {
        None = 0u,
        Enabled = 1u << 0,
    };

    struct alignas(16) WaterSurfaceWaveGpuData {
        // (direction.x, direction.y, amplitude, waveNumber)
        MATH::Vec4 directionAmplitudeWaveNumber{ 0.0f, 0.0f, 0.0f, 0.0f };
        // (angularFrequency, steepness, reserved, reserved)
        MATH::Vec4 angularFrequencySteepness{ 0.0f, 0.0f, 0.0f, 0.0f };
    };

    // HLSL の HikariWaterSurfaceGpuData と同一の sparse record layout。
    // index 0 は必ず無効 sentinel とし、SurfaceGpuSceneInstance は index のみを保持する。
    struct alignas(16) WaterSurfaceGpuData {
        uint32_t schemaVersion = 0u;
        uint32_t flags = 0u;
        uint32_t waveCount = 0u;
        // index 0 sentinel のみが使用する、sentinel 自身を含む有効 record 数。
        // Root SRV は要素数を保持しないため、shader 側の bounds 判定に使う。
        uint32_t recordCount = 0u;
        MATH::Vec4 surfaceParams{ 0.0f, 0.0f, 0.0f, 0.0f };
        MATH::Vec4 depthTransmittanceFoam{ 0.0f, 0.0f, 0.0f, 0.0f };
        MATH::Vec4 scatteringAbsorption{ 0.0f, 0.0f, 0.0f, 0.0f };
        MATH::Vec4 foamRoughness{ 0.0f, 0.0f, 0.0f, 0.0f };
        MATH::Vec4 coverageParams{ 0.0f, 0.0f, 0.0f, 0.0f };
        std::array<WaterSurfaceWaveGpuData, kWaterSurfaceWaveCount> waves{};
    };

    static_assert(sizeof(WaterSurfaceWaveGpuData) == 32u);
    static_assert(alignof(WaterSurfaceWaveGpuData) == 16u);
    static_assert(offsetof(WaterSurfaceGpuData, surfaceParams) == 16u);
    static_assert(offsetof(WaterSurfaceGpuData, recordCount) == 12u);
    static_assert(offsetof(WaterSurfaceGpuData, coverageParams) == 80u);
    static_assert(offsetof(WaterSurfaceGpuData, waves) == 96u);
    static_assert(sizeof(WaterSurfaceGpuData) == 224u);
    static_assert(alignof(WaterSurfaceGpuData) == 16u);

    WaterSurfaceGpuData BuildWaterSurfaceGpuDataSentinel(
        uint32_t recordCount = 1u) noexcept;
    WaterSurfaceGpuData BuildWaterSurfaceGpuData(
        const WaterSurfaceRenderData& source,
        uint32_t liquidCoverageOffset) noexcept;

} // namespace HIKARI::RENDER3D::WATER
