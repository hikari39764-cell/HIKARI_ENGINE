#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <d3d12.h>

#include "Render3D/Resources/HIKARI_RenderResourcePool.h"

namespace HIKARI::RENDER3D {

    // Render3D は typed resource handle を使い、backend の int handle を外へ広げない。
    enum class TextureResourceColorSpace : uint8_t {
        Auto,
        Linear,
        Srgb,
    };

    enum class TextureResourceDimension : uint8_t {
        Unknown,
        Texture2D,
        TextureCube,
    };

    struct TextureResourceSystemStats {
        RenderResourcePoolStats pool{};
        uint32_t backendUsedDescriptorCount = 0;
        uint32_t backendFreeDescriptorCount = 0;
        uint32_t backendMaxDescriptorCount = 0;
    };

    struct TextureResourceLoadRequest {
        std::string name{};
        std::string path{};
        TextureResourceColorSpace colorSpace = TextureResourceColorSpace::Auto;
    };

    struct TextureResourceBatchLoadStats {
        uint32_t requested = 0;
        uint32_t cacheHits = 0;
        uint32_t uploaded = 0;
        uint32_t fallbackLoads = 0;
        uint32_t failed = 0;
        uint32_t registered = 0;
        uint64_t uploadedBytes = 0;
    };

    ID3D12DescriptorHeap* GetTextureResourceSrvHeap();
    TextureResourceSystemStats GetTextureResourceSystemStats();

    TextureResourceHandle LoadTextureResource(
        const std::string& name,
        const std::string& path);

    TextureResourceHandle LoadTextureResourceWithColorSpace(
        const std::string& name,
        const std::string& path,
        TextureResourceColorSpace colorSpace);

    TextureResourceHandle FindLoadedTextureResourceWithColorSpace(
        const std::string& name,
        const std::string& path,
        TextureResourceColorSpace colorSpace);

    std::vector<TextureResourceHandle> PreloadTextureResourcesWithColorSpace(
        const std::vector<TextureResourceLoadRequest>& requests,
        TextureResourceBatchLoadStats* outStats = nullptr);

    TextureResourceHandle LoadTextureResourceSrgb(
        const std::string& name,
        const std::string& path);

    TextureResourceHandle LoadTextureResourceLinear(
        const std::string& name,
        const std::string& path);

    TextureResourceHandle LoadCubemapResource(
        const std::string& name,
        const std::string& path,
        TextureResourceColorSpace colorSpace = TextureResourceColorSpace::Linear);

    TextureResourceHandle CreateSolidColorTextureResource(
        const std::string& name,
        uint32_t rgba,
        TextureResourceColorSpace colorSpace = TextureResourceColorSpace::Linear);

    TextureResourceHandle CreateSolidColorCubemapResource(
        const std::string& name,
        uint32_t rgba,
        TextureResourceColorSpace colorSpace = TextureResourceColorSpace::Linear);

    TextureResourceHandle CreateCheckerTextureResource(
        const std::string& name,
        uint32_t colorA,
        uint32_t colorB,
        TextureResourceColorSpace colorSpace = TextureResourceColorSpace::Linear);

    TextureResourceHandle RegisterTextureResourceFromNative(
        ID3D12Resource* resource,
        DXGI_FORMAT srvFormat,
        RenderResourceDesc desc = {});

    TextureResourceHandle RegisterCubeTextureResourceFromNative(
        ID3D12Resource* resource,
        DXGI_FORMAT srvFormat,
        RenderResourceDesc desc = {});

    TextureResourceHandle RegisterTextureResourceFromBackendHandle(
        int backendHandle,
        RenderResourceDesc desc = {});

    TextureResourceHandle FindTextureResourceFromBackendHandle(int backendHandle);

    bool ReleaseTextureResource(TextureResourceHandle handle);
    bool IsTextureResourceValid(TextureResourceHandle handle);

    int GetTextureResourceBackendHandle(TextureResourceHandle handle);
    ID3D12Resource* GetTextureResourceNative(TextureResourceHandle handle);
    D3D12_CPU_DESCRIPTOR_HANDLE GetTextureResourceSrvCpuHandle(TextureResourceHandle handle);
    D3D12_GPU_DESCRIPTOR_HANDLE GetTextureResourceSrvGpuHandle(TextureResourceHandle handle);
    UINT GetTextureResourceSrvDescriptorIndex(TextureResourceHandle handle);
    D3D12_GPU_DESCRIPTOR_HANDLE GetTextureResourceSrvGpuHandleFromBackendHandle(int backendHandle);
    UINT GetTextureResourceSrvDescriptorIndexFromBackendHandle(int backendHandle);
    UINT GetTextureResourceMipCount(TextureResourceHandle handle);
    DXGI_FORMAT GetTextureResourceFormat(TextureResourceHandle handle);
    TextureResourceDimension GetTextureResourceDimension(TextureResourceHandle handle);
    const RenderResourceRecord* GetTextureResourceRecord(TextureResourceHandle handle);

} // namespace HIKARI::RENDER3D
