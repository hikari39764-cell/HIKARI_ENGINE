#pragma once

#include <d3d12.h>

#include "Gfx/HIKARI_GfxContext.h"

namespace HIKARI::RENDER3D {

    D3D12_GPU_DESCRIPTOR_HANDLE GetMaterialTexturePoolSrvGpuHandle(
        const GFX::Context& context);

    D3D12_GPU_DESCRIPTOR_HANDLE GetClusterGeometryPoolSrvGpuHandle(
        const GFX::Context& context);

} // namespace HIKARI::RENDER3D
