#pragma once

#include <d3d12.h>

#include "Gfx/HIKARI_GfxContext.h"

namespace HIKARI::RENDER3D {

D3D12_GPU_DESCRIPTOR_HANDLE GetMaterialTexturePoolSrvGpuHandle(const GFX::Context& context);

// Base of the dedicated ClusterGeometry descriptor arena; it is not the
// general dynamic system SRV range.
D3D12_GPU_DESCRIPTOR_HANDLE GetClusterGeometryPoolSrvGpuHandle(const GFX::Context& context);

} // namespace HIKARI::RENDER3D
