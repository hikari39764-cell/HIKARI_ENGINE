#pragma once

#include <cstdint>

#include <d3d12.h>

namespace HIKARI::RENDER3D {
    // Borrowed GPU texture. The producer owns allocation, transitions, and release.
    struct RenderTextureView {
        ID3D12Resource* resource = nullptr;
        D3D12_GPU_DESCRIPTOR_HANDLE srv{};
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t mipCount = 0;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
        bool valid = false;
        bool IsSampleable() const noexcept {
            return valid && srv.ptr != 0 && width != 0 && height != 0;
        }
    };
} // namespace HIKARI::RENDER3D
