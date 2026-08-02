#include "Render3D/Lighting/HIKARI_IblEnvironment.h"

#include <algorithm>
#include <sstream>

#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::IBL {

    namespace {
        IblEnvironmentData gData{};
        ScopedEnvironmentOverride* gActiveOverride = nullptr;
        struct IblStateKey {
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
            uint32_t prefilteredMipCount = 1;
            uint32_t irradianceMipCount = 0;
            uint32_t prefilteredActualMipCount = 0;
            uint32_t brdfLutMipCount = 0;
            DXGI_FORMAT irradianceFormat = DXGI_FORMAT_UNKNOWN;
            DXGI_FORMAT prefilteredFormat = DXGI_FORMAT_UNKNOWN;
            DXGI_FORMAT brdfLutFormat = DXGI_FORMAT_UNKNOWN;
            bool prefilteredMipMismatch = false;
        };

        bool gHasLastIblKey = false;
        IblStateKey gLastIblKey{};
        uint32_t gRequestedPrefilteredMipCount = 1;

        bool operator==(const IblStateKey& lhs, const IblStateKey& rhs) {
            return lhs.valid == rhs.valid &&
                lhs.hasIrradiance == rhs.hasIrradiance &&
                lhs.hasPrefiltered == rhs.hasPrefiltered &&
                lhs.hasBrdfLut == rhs.hasBrdfLut &&
                lhs.irradianceResource == rhs.irradianceResource &&
                lhs.prefilteredResource == rhs.prefilteredResource &&
                lhs.brdfLutResource == rhs.brdfLutResource &&
                lhs.irradianceHandle == rhs.irradianceHandle &&
                lhs.prefilteredHandle == rhs.prefilteredHandle &&
                lhs.brdfLutHandle == rhs.brdfLutHandle &&
                lhs.prefilteredMipCount == rhs.prefilteredMipCount &&
                lhs.irradianceMipCount == rhs.irradianceMipCount &&
                lhs.prefilteredActualMipCount == rhs.prefilteredActualMipCount &&
                lhs.brdfLutMipCount == rhs.brdfLutMipCount &&
                lhs.irradianceFormat == rhs.irradianceFormat &&
                lhs.prefilteredFormat == rhs.prefilteredFormat &&
                lhs.brdfLutFormat == rhs.brdfLutFormat &&
                lhs.prefilteredMipMismatch == rhs.prefilteredMipMismatch;
        }

        IblStateKey MakeIblStateKey() {
            IblStateKey key{};
            key.valid = gData.valid;
            key.hasIrradiance = gData.hasIrradiance;
            key.hasPrefiltered = gData.hasPrefiltered;
            key.hasBrdfLut = gData.hasBrdfLut;
            key.irradianceResource = gData.irradianceResource;
            key.prefilteredResource = gData.prefilteredResource;
            key.brdfLutResource = gData.brdfLutResource;
            key.irradianceHandle = gData.irradianceHandle;
            key.prefilteredHandle = gData.prefilteredHandle;
            key.brdfLutHandle = gData.brdfLutHandle;
            key.prefilteredMipCount = gData.prefilteredMipCount;
            key.irradianceMipCount = gData.irradianceMipCount;
            key.prefilteredActualMipCount = gData.prefilteredActualMipCount;
            key.brdfLutMipCount = gData.brdfLutMipCount;
            key.irradianceFormat = gData.irradianceFormat;
            key.prefilteredFormat = gData.prefilteredFormat;
            key.brdfLutFormat = gData.brdfLutFormat;
            key.prefilteredMipMismatch = gData.prefilteredMipMismatch;
            return key;
        }

        const char* BoolText(bool value) {
            return value ? "true" : "false";
        }

        bool IsValidSrv(D3D12_GPU_DESCRIPTOR_HANDLE handle) {
            return handle.ptr != 0;
        }

        void RefreshResolvedHandles(
            IblEnvironmentData& data,
            uint32_t requestedPrefilteredMipCount) {

            data.irradianceHandle =
                RENDER3D::GetTextureResourceBackendHandle(data.irradianceResource);
            data.prefilteredHandle =
                RENDER3D::GetTextureResourceBackendHandle(data.prefilteredResource);
            data.brdfLutHandle =
                RENDER3D::GetTextureResourceBackendHandle(data.brdfLutResource);

            data.irradianceSrv =
                RENDER3D::GetTextureResourceSrvGpuHandle(data.irradianceResource);
            data.prefilteredSrv =
                RENDER3D::GetTextureResourceSrvGpuHandle(data.prefilteredResource);
            data.brdfLutSrv =
                RENDER3D::GetTextureResourceSrvGpuHandle(data.brdfLutResource);

            data.irradianceMipCount =
                RENDER3D::GetTextureResourceMipCount(data.irradianceResource);
            data.prefilteredActualMipCount =
                RENDER3D::GetTextureResourceMipCount(data.prefilteredResource);
            data.brdfLutMipCount =
                RENDER3D::GetTextureResourceMipCount(data.brdfLutResource);
            data.irradianceFormat =
                RENDER3D::GetTextureResourceFormat(data.irradianceResource);
            data.prefilteredFormat =
                RENDER3D::GetTextureResourceFormat(data.prefilteredResource);
            data.brdfLutFormat =
                RENDER3D::GetTextureResourceFormat(data.brdfLutResource);

            // IBL は SRV だけでなく dimension/mip も満たした時だけ有効にする。
            data.hasIrradiance = IsValidSrv(data.irradianceSrv) &&
                RENDER3D::GetTextureResourceDimension(data.irradianceResource) ==
                    RENDER3D::TextureResourceDimension::TextureCube &&
                data.irradianceMipCount >= 1;
            data.hasPrefiltered = IsValidSrv(data.prefilteredSrv) &&
                RENDER3D::GetTextureResourceDimension(data.prefilteredResource) ==
                    RENDER3D::TextureResourceDimension::TextureCube &&
                data.prefilteredActualMipCount >= 2;
            data.hasBrdfLut = IsValidSrv(data.brdfLutSrv) &&
                RENDER3D::GetTextureResourceDimension(data.brdfLutResource) ==
                    RENDER3D::TextureResourceDimension::Texture2D &&
                data.brdfLutMipCount >= 1;

            const uint32_t requestedMipCount =
                std::max<uint32_t>(1u, requestedPrefilteredMipCount);
            if (data.hasPrefiltered) {
                data.prefilteredMipCount = std::max<uint32_t>(
                    1u,
                    std::min<uint32_t>(requestedMipCount, data.prefilteredActualMipCount));
                data.prefilteredMipMismatch =
                    requestedMipCount != data.prefilteredActualMipCount;
            }
            else {
                data.prefilteredMipCount = 1;
                data.prefilteredMipMismatch = false;
            }

            data.valid = data.hasIrradiance ||
                data.hasPrefiltered ||
                data.hasBrdfLut;
        }

        void LogIblStateIfChanged() {
            const IblStateKey key = MakeIblStateKey();
            if (gHasLastIblKey && key == gLastIblKey) {
                return;
            }

            // IBL の状態変化だけを log に出し、毎 frame の spam を避ける。
            gLastIblKey = key;
            gHasLastIblKey = true;

            std::ostringstream oss;
            oss << "[Environment][IBL] state changed"
                << " valid=" << BoolText(gData.valid)
                << " irradiance=" << BoolText(gData.hasIrradiance)
                << " prefiltered=" << BoolText(gData.hasPrefiltered)
                << " brdf=" << BoolText(gData.hasBrdfLut)
                << " handles=" << gData.irradianceHandle << "/" << gData.prefilteredHandle << "/" << gData.brdfLutHandle
                << " mips=" << gData.prefilteredMipCount
                << " actualMips=" << gData.irradianceMipCount << "/" << gData.prefilteredActualMipCount << "/" << gData.brdfLutMipCount
                << " formats=" << GFX::FormatToString(gData.irradianceFormat) << "/"
                << GFX::FormatToString(gData.prefilteredFormat) << "/"
                << GFX::FormatToString(gData.brdfLutFormat)
                << " mipMismatch=" << BoolText(gData.prefilteredMipMismatch);
            HIKARI_LOG_INFO(oss.str());

            if (gData.irradianceHandle >= 0 && !gData.hasIrradiance) {
                HIKARI_LOG_WARN("[Environment][IBL] irradiance is invalid.");
            }
            if (gData.prefilteredHandle >= 0 && !gData.hasPrefiltered) {
                HIKARI_LOG_WARN("[Environment][IBL] prefiltered is invalid.");
            }
            if (gData.brdfLutHandle >= 0 && !gData.hasBrdfLut) {
                HIKARI_LOG_WARN("[Environment][IBL] BRDF LUT is invalid.");
            }
            if (gData.prefilteredMipMismatch) {
                HIKARI_LOG_WARN(
                    "[Environment][IBL] prefiltered mip count mismatch. requested=" +
                    std::to_string(std::max<uint32_t>(1u, gRequestedPrefilteredMipCount)) +
                    " actual=" + std::to_string(gData.prefilteredActualMipCount));
            }
        }
    }

    void Reset() {
        gData = {};
        gRequestedPrefilteredMipCount = 1;
        RefreshResolvedHandles(gData, gRequestedPrefilteredMipCount);
    }

    void SetFromTextureResources(
        RENDER3D::TextureResourceHandle irradianceResource,
        RENDER3D::TextureResourceHandle prefilteredResource,
        RENDER3D::TextureResourceHandle brdfLutResource,
        uint32_t prefilteredMipCount) {
        gData.irradianceResource = irradianceResource;
        gData.prefilteredResource = prefilteredResource;
        gData.brdfLutResource = brdfLutResource;
        gRequestedPrefilteredMipCount = std::max<uint32_t>(1u, prefilteredMipCount);
        RefreshResolvedHandles(gData, gRequestedPrefilteredMipCount);
        LogIblStateIfChanged();
    }

    void SetFromTextureHandles(
        int irradianceHandle,
        int prefilteredHandle,
        int brdfLutHandle,
        uint32_t prefilteredMipCount) {
        SetFromTextureResources(
            RENDER3D::RegisterTextureResourceFromBackendHandle(irradianceHandle),
            RENDER3D::RegisterTextureResourceFromBackendHandle(prefilteredHandle),
            RENDER3D::RegisterTextureResourceFromBackendHandle(brdfLutHandle),
            prefilteredMipCount);
    }

    ScopedEnvironmentOverride::ScopedEnvironmentOverride(
        IblEnvironmentSource source) noexcept
        : source_(source),
          previous_(gActiveOverride) {

        resolved_.irradianceResource = source_.irradianceResource;
        resolved_.prefilteredResource = source_.prefilteredResource;
        resolved_.brdfLutResource = source_.brdfLutResource;
        gActiveOverride = this;
    }

    ScopedEnvironmentOverride::~ScopedEnvironmentOverride() {
        if (gActiveOverride == this) {
            gActiveOverride = previous_;
        }
    }

    const IblEnvironmentData& GetEnvironmentData() {
        if (gActiveOverride != nullptr) {
            RefreshResolvedHandles(
                gActiveOverride->resolved_,
                gActiveOverride->source_.prefilteredMipCount);
            return gActiveOverride->resolved_;
        }

        RefreshResolvedHandles(gData, gRequestedPrefilteredMipCount);
        LogIblStateIfChanged();
        return gData;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetIrradianceSrv() {
        return GetEnvironmentData().irradianceSrv;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetPrefilteredSrv() {
        return GetEnvironmentData().prefilteredSrv;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetBrdfLutSrv() {
        return GetEnvironmentData().brdfLutSrv;
    }

} // namespace HIKARI::IBL
