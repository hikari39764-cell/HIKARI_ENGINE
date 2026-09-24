#pragma once

#include <cstddef>
#include <cstdint>

#include "Render3D/Sand/HIKARI_SandSurfaceGpuData.h"
#include "Render3D/Terrain/HIKARI_TerrainGpuFrameBuffer.h"

namespace HIKARI::RENDER3D::SAND {

struct SandSurfaceFrameBufferTraits {
    static constexpr std::size_t kRecordCapacity = kSandSurfaceMaxGpuDataRecords;
    static constexpr std::size_t kHeightCapacity = kSandSurfaceMaxHeightCells;

    SandSurfaceGpuData MakeSentinel(std::size_t count) const noexcept {
        SandSurfaceGpuData sentinel = BuildSandSurfaceGpuDataSentinel();
        sentinel.heightOffset = static_cast<uint32_t>(count);
        return sentinel;
    }
};

using SandSurfaceGpuFrameBuffer =
    RENDER3D::TERRAIN::SurfaceGpuFrameBuffer<SandSurfaceGpuData, SandSurfaceFrameBufferTraits>;

} // namespace HIKARI::RENDER3D::SAND
