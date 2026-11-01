#pragma once
#include <d3d12.h>

namespace HIKARI::RENDER3D {
// Borrowed from the current view; surface passes never upload another copy.
struct SurfaceLightingBindings {
    D3D12_GPU_VIRTUAL_ADDRESS light = 0, shadow = 0, skyEnvironment = 0;
    D3D12_GPU_VIRTUAL_ADDRESS localLights = 0, localLightHeaders = 0, localLightIndices = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE shadowMap{}, irradiance{}, prefiltered{};
    bool IsValid() const noexcept {
        return light && shadow && skyEnvironment && localLights && localLightHeaders && localLightIndices &&
               shadowMap.ptr && irradiance.ptr && prefiltered.ptr;
    }
};
} // namespace HIKARI::RENDER3D
