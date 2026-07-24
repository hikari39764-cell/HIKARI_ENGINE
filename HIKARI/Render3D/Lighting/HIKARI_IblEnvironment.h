#pragma once

#include <cstdint>
#include <d3d12.h>
#include <dxgiformat.h>

#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"

namespace HIKARI::IBL {

    struct IblEnvironmentData {
        bool valid = false;
        bool hasIrradiance = false;
        bool hasPrefiltered = false;
        bool hasBrdfLut = false;

        RENDER3D::TextureResourceHandle irradianceResource{};
        RENDER3D::TextureResourceHandle prefilteredResource{};
        RENDER3D::TextureResourceHandle brdfLutResource{};

        int irradianceHandle = -1;
        int prefilteredHandle = -1;
        int brdfLutHandle = -1;

        D3D12_GPU_DESCRIPTOR_HANDLE irradianceSrv{};
        D3D12_GPU_DESCRIPTOR_HANDLE prefilteredSrv{};
        D3D12_GPU_DESCRIPTOR_HANDLE brdfLutSrv{};

        uint32_t prefilteredMipCount = 1;
        uint32_t irradianceMipCount = 0;
        uint32_t prefilteredActualMipCount = 0;
        uint32_t brdfLutMipCount = 0;
        DXGI_FORMAT irradianceFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT prefilteredFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT brdfLutFormat = DXGI_FORMAT_UNKNOWN;
        bool prefilteredMipMismatch = false;
    };

    void Reset();

    void SetFromTextureResources(
        RENDER3D::TextureResourceHandle irradianceResource,
        RENDER3D::TextureResourceHandle prefilteredResource,
        RENDER3D::TextureResourceHandle brdfLutResource,
        uint32_t prefilteredMipCount);

    void SetFromTextureHandles(
        int irradianceHandle,
        int prefilteredHandle,
        int brdfLutHandle,
        uint32_t prefilteredMipCount);

    const IblEnvironmentData& GetEnvironmentData();

    D3D12_GPU_DESCRIPTOR_HANDLE GetIrradianceSrv();
    D3D12_GPU_DESCRIPTOR_HANDLE GetPrefilteredSrv();
    D3D12_GPU_DESCRIPTOR_HANDLE GetBrdfLutSrv();

} // namespace HIKARI::IBL
