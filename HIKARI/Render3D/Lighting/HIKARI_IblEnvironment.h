#pragma once

#include <cstdint>
#include <string_view>
#include <d3d12.h>
#include <dxgiformat.h>

#include "Render3D/Lighting/HIKARI_SkyAsset.h"
#include "RenderCore/Resources/HIKARI_RenderResourceHandle.h"

namespace HIKARI::GFX {
struct Context;
}
namespace HIKARI::ATMOSPHERE {
struct AtmosphereLutConstants;
}

namespace HIKARI::IBL {
struct EnvironmentLightingCacheStatus;

struct IblEnvironmentSource {
    RENDER3D::TextureResourceHandle irradianceResource{};
    RENDER3D::TextureResourceHandle prefilteredResource{};
    RENDER3D::TextureResourceHandle brdfLutResource{};
    uint32_t prefilteredMipCount = 1;

    constexpr bool IsConfigured() const noexcept {
        return irradianceResource.IsValid() || prefilteredResource.IsValid() || brdfLutResource.IsValid();
    }

    bool IsComplete() const noexcept;
};

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
void BeginEnvironmentFrame();
void PrepareEnvironmentLighting(const GFX::Context& graphics, SkyMode mode, std::string_view skyAsset,
                                const ATMOSPHERE::AtmosphereLutConstants& atmosphere,
                                RENDER3D::TextureResourceHandle source, D3D12_GPU_DESCRIPTOR_HANDLE atmosphereSkyView,
                                D3D12_GPU_DESCRIPTOR_HANDLE cloudSky, uint64_t cloudSkyVersion, float nightSkyLuminance,
                                uint64_t frameIndex);
void ShutdownEnvironmentLighting(const GFX::Context& graphics);

// グローバル設定を反映し、dimension/mip 検証後に三つの要素が揃っているかを返す。
// ScopedEnvironmentOverride の有無はこの判定に影響しない。
bool SetFromTextureResources(RENDER3D::TextureResourceHandle irradianceResource,
                             RENDER3D::TextureResourceHandle prefilteredResource,
                             RENDER3D::TextureResourceHandle brdfLutResource, uint32_t prefilteredMipCount);

class ScopedEnvironmentOverride final {
public:
    explicit ScopedEnvironmentOverride(IblEnvironmentSource source) noexcept;
    ~ScopedEnvironmentOverride();

    ScopedEnvironmentOverride(const ScopedEnvironmentOverride&) = delete;
    ScopedEnvironmentOverride& operator=(const ScopedEnvironmentOverride&) = delete;

private:
    friend const IblEnvironmentData& GetEnvironmentData();

    IblEnvironmentSource source_{};
    IblEnvironmentData resolved_{};
    ScopedEnvironmentOverride* previous_ = nullptr;
};

const IblEnvironmentData& GetEnvironmentData();

D3D12_GPU_DESCRIPTOR_HANDLE GetIrradianceSrv();
D3D12_GPU_DESCRIPTOR_HANDLE GetPrefilteredSrv();
D3D12_GPU_DESCRIPTOR_HANDLE GetBrdfLutSrv();

} // namespace HIKARI::IBL
