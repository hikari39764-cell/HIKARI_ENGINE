#pragma once

#include <cstddef>
#include <cstdint>

#include "Render3D/Terrain/HIKARI_TerrainSurfaceGpuData.h"
#include "Render3D/Terrain/HIKARI_TerrainGpuFrameBuffer.h"

namespace HIKARI::RENDER3D::TERRAIN {

struct TerrainSurfaceFrameBufferTraits {
    static constexpr std::size_t kRecordCapacity = kTerrainSurfaceMaxGpuDataRecords;
    static constexpr std::size_t kHeightCapacity = kTerrainSurfaceMaxHeightCells;

    TerrainSurfaceGpuData MakeSentinel(std::size_t count) const noexcept {
        TerrainSurfaceGpuData sentinel = BuildTerrainSurfaceGpuDataSentinel();
        sentinel.heightOffset = static_cast<uint32_t>(count);
        return sentinel;
    }
};

using TerrainSurfaceGpuFrameBuffer =
    RENDER3D::TERRAIN::SurfaceGpuFrameBuffer<TerrainSurfaceGpuData, TerrainSurfaceFrameBufferTraits>;

} // namespace HIKARI::RENDER3D::TERRAIN
