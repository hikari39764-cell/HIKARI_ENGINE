#pragma once

#include <cstdint>
#include <vector>

#include <d3d12.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Resources/HIKARI_RenderResourcePool.h"

namespace HIKARI::RENDER3D {

    struct RenderResourceDescriptorPoolStats {
        bool initialized = false;
        uint32_t begin = 0;
        uint32_t capacity = 0;
        uint32_t used = 0;
        uint32_t free = 0;
        uint32_t allocationCount = 0;
        uint32_t freeCount = 0;
        uint32_t failedAllocationCount = 0;
    };

    void UpdateRenderResourceDescriptorPoolContext(const GFX::Context& ctx);
    void ShutdownRenderResourceDescriptorPool();

    RenderResourceView AllocateRenderResourceDescriptor();
    std::vector<RenderResourceView>
        AllocateRenderResourceDescriptorRange(uint32_t count);

    RenderResourceView AllocateBufferSrvDescriptor(
        ID3D12Resource* resource,
        DXGI_FORMAT format,
        UINT numElements,
        UINT structureByteStride,
        D3D12_BUFFER_SRV_FLAGS flags = D3D12_BUFFER_SRV_FLAG_NONE);

    RenderResourceView AllocateBufferUavDescriptor(
        ID3D12Resource* resource,
        DXGI_FORMAT format,
        UINT numElements,
        UINT structureByteStride,
        ID3D12Resource* counterResource = nullptr,
        UINT64 counterOffsetBytes = 0u,
        D3D12_BUFFER_UAV_FLAGS flags = D3D12_BUFFER_UAV_FLAG_NONE);

    RenderResourceView AllocateTexture2DSrvDescriptor(
        ID3D12Resource* resource,
        DXGI_FORMAT format,
        UINT mostDetailedMip = 0,
        UINT mipLevels = 1);

    RenderResourceView AllocateTexture2DUavDescriptor(
        ID3D12Resource* resource,
        DXGI_FORMAT format,
        UINT mipSlice = 0);

    RenderResourceView AllocateTexture3DSrvDescriptor(
        ID3D12Resource* resource,
        DXGI_FORMAT format,
        UINT mostDetailedMip = 0,
        UINT mipLevels = 1);

    RenderResourceView AllocateTexture3DUavDescriptor(
        ID3D12Resource* resource,
        DXGI_FORMAT format,
        UINT mipSlice = 0,
        UINT firstWSlice = 0,
        UINT wSize = UINT_MAX);

    bool ReleaseRenderResourceDescriptor(RenderResourceView view);

    RenderResourceDescriptorPoolStats GetRenderResourceDescriptorPoolStats();

} // namespace HIKARI::RENDER3D
