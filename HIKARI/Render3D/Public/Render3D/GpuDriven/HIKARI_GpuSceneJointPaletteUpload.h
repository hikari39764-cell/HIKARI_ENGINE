#pragma once

#include <cstdint>
#include <d3d12.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneJointPaletteSnapshot.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

struct GpuSceneJointPaletteUploadStats {
    uint64_t matrixPayloadBytes = 0u;
    uint64_t snapshotUploadCount = 0u;
    uint64_t snapshotReuseCount = 0u;
    uint64_t emptyBindingUploadCount = 0u;
};

D3D12_GPU_VIRTUAL_ADDRESS UploadGpuSceneJointPaletteSnapshot(const GFX::Context& graphics,
                                                             const GpuSceneJointPaletteSnapshotPtr& snapshot);
GpuSceneJointPaletteUploadStats GetGpuSceneJointPaletteUploadStats(const GFX::Context& graphics);
void FinalizeGpuSceneJointPaletteUploads(const GFX::Context& graphics);

} // namespace HIKARI::RENDER3D::GPUDRIVEN
