#pragma once

#include <cstddef>
#include <cstdint>

#include "Render3D/Terrain/HIKARI_TerrainGpuFrameBuffer.h"
#include "Render3D/Water/HIKARI_WaterSurfaceGpuData.h"

namespace HIKARI::RENDER3D::WATER {

    constexpr std::size_t kDefaultWaterSurfaceGpuDataCapacity = 8193u;

    struct WaterSurfaceFrameBufferTraits {
        static constexpr std::size_t kRecordCapacity =
            kDefaultWaterSurfaceGpuDataCapacity;
        static constexpr std::size_t kHeightCapacity = 1u;

        WaterSurfaceGpuData MakeSentinel(
            std::size_t count) const noexcept {
            return BuildWaterSurfaceGpuDataSentinel(
                static_cast<uint32_t>(count));
        }
    };

    class WaterSurfaceGpuFrameBuffer final :
        public RENDER3D::TERRAIN::SurfaceGpuFrameBuffer<
            WaterSurfaceGpuData,
            WaterSurfaceFrameBufferTraits> {
    };

} // namespace HIKARI::RENDER3D::WATER
