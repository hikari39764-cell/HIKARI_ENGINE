#include "Render3D/Lighting/HIKARI_IblEnvironment.h"

#include <algorithm>
#include <sstream>

#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::IBL {

    namespace {
        IblEnvironmentData gData{};
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

        void RefreshResolvedHandles() {
            gData.irradianceHandle =
                RENDER3D::GetTextureResourceBackendHandle(gData.irradianceResource);
            gData.prefilteredHandle =
                RENDER3D::GetTextureResourceBackendHandle(gData.prefilteredResource);
            gData.brdfLutHandle =
                RENDER3D::GetTextureResourceBackendHandle(gData.brdfLutResource);

            gData.irradianceSrv =
                RENDER3D::GetTextureResourceSrvGpuHandle(gData.irradianceResource);
            gData.prefilteredSrv =
                RENDER3D::GetTextureResourceSrvGpuHandle(gData.prefilteredResource);
            gData.brdfLutSrv =
                RENDER3D::GetTextureResourceSrvGpuHandle(gData.brdfLutResource);

            gData.irradianceMipCount =
                RENDER3D::GetTextureResourceMipCount(gData.irradianceResource);
            gData.prefilteredActualMipCount =
                RENDER3D::GetTextureResourceMipCount(gData.prefilteredResource);
            gData.brdfLutMipCount =
                RENDER3D::GetTextureResourceMipCount(gData.brdfLutResource);
            gData.irradianceFormat =
                RENDER3D::GetTextureResourceFormat(gData.irradianceResource);
            gData.prefilteredFormat =
                RENDER3D::GetTextureResourceFormat(gData.prefilteredResource);
            gData.brdfLutFormat =
                RENDER3D::GetTextureResourceFormat(gData.brdfLutResource);

            // IBL は SRV だけでなく dimension/mip も満たした時だけ有効にする。
            gData.hasIrradiance = IsValidSrv(gData.irradianceSrv) &&
                RENDER3D::GetTextureResourceDimension(gData.irradianceResource) ==
                    RENDER3D::TextureResourceDimension::TextureCube &&
                gData.irradianceMipCount >= 1;
            gData.hasPrefiltered = IsValidSrv(gData.prefilteredSrv) &&
                RENDER3D::GetTextureResourceDimension(gData.prefilteredResource) ==
                    RENDER3D::TextureResourceDimension::TextureCube &&
                gData.prefilteredActualMipCount >= 2;
            gData.hasBrdfLut = IsValidSrv(gData.brdfLutSrv) &&
                RENDER3D::GetTextureResourceDimension(gData.brdfLutResource) ==
                    RENDER3D::TextureResourceDimension::Texture2D &&
                gData.brdfLutMipCount >= 1;

            const uint32_t requestedMipCount = std::max<uint32_t>(1u, gRequestedPrefilteredMipCount);
            if (gData.hasPrefiltered) {
                gData.prefilteredMipCount = std::max<uint32_t>(
                    1u,
                    std::min<uint32_t>(requestedMipCount, gData.prefilteredActualMipCount));
                gData.prefilteredMipMismatch = requestedMipCount != gData.prefilteredActualMipCount;
            }
            else {
                gData.prefilteredMipCount = 1;
                gData.prefilteredMipMismatch = false;
            }

            gData.valid = gData.hasIrradiance || gData.hasPrefiltered || gData.hasBrdfLut;
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
        RefreshResolvedHandles();
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
        RefreshResolvedHandles();
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

    const IblEnvironmentData& GetEnvironmentData() {
        RefreshResolvedHandles();
        LogIblStateIfChanged();
        return gData;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetIrradianceSrv() {
        RefreshResolvedHandles();
        LogIblStateIfChanged();
        return gData.irradianceSrv;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetPrefilteredSrv() {
        RefreshResolvedHandles();
        LogIblStateIfChanged();
        return gData.prefilteredSrv;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetBrdfLutSrv() {
        RefreshResolvedHandles();
        LogIblStateIfChanged();
        return gData.brdfLutSrv;
    }

} // namespace HIKARI::IBL
