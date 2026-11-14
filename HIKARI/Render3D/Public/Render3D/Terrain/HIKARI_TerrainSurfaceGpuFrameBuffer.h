#pragma once

#include <cstddef>
#include <cstdint>

#include "Render3D/Terrain/HIKARI_TerrainSurfaceGpuData.h"
#include "Render3D/Runtime/HIKARI_SceneSurfaceFloatPool.h"
#include "Render3D/Surface/HIKARI_SurfaceGpuFrameBuffer.h"

namespace HIKARI::RENDER3D::TERRAIN {

struct TerrainSurfaceFrameBufferTraits {
    static constexpr std::size_t kRecordCapacity = kTerrainSurfaceMaxGpuDataRecords;
    static constexpr std::size_t kHeightCapacity = RUNTIME::kSurfaceFeatureFloatPoolCapacity;

    TerrainSurfaceGpuData MakeSentinel(std::size_t count) const noexcept {
        return BuildTerrainSurfaceGpuDataSentinel(static_cast<uint32_t>(count));
    }
};

using TerrainSurfaceGpuFrameBuffer =
    RENDER3D::SURFACE::SurfaceGpuFrameBuffer<TerrainSurfaceGpuData, TerrainSurfaceFrameBufferTraits>;

} // namespace HIKARI::RENDER3D::TERRAIN
