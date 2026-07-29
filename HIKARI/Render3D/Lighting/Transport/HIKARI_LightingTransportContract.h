#pragma once

#include <cstdint>

#include "Assets/Lighting/HIKARI_LightingBakePolicy.h"

namespace HIKARI::RENDER3D::LIGHTING::TRANSPORT {

    enum class LightingTransportBackendKind : uint8_t {
        RasterCapture,
        DxrPathTracing,
    };

    enum class LightingTransportProductKind : uint8_t {
        SpecularReflection,
        DiffuseIrradiance,
        SurfaceLightmap,
    };

    struct LightingTransportBackendCapabilities {
        LightingTransportBackendKind backend =
            LightingTransportBackendKind::RasterCapture;
        bool available = false;
        bool supportsSpecularReflection = false;
        bool supportsDiffuseIrradiance = false;
        bool supportsSurfaceLightmaps = false;
        bool supportsProgressiveSampling = false;
        bool supportsMultipleBounces = false;
        uint32_t maximumBounceCount = 0u;
    };

    struct LightingTransportBakeSettings {
        uint32_t sampleCount =
            ASSETS::LIGHTING::
                kSurfaceLightmapDefaultSampleCount;
        uint32_t bounceCount =
            ASSETS::LIGHTING::
                kSurfaceLightmapDefaultBounceCount;
        uint32_t outputResolution =
            ASSETS::LIGHTING::
                kSurfaceLightmapDefaultResolution;
    };

    const char* ToString(
        LightingTransportBackendKind backend) noexcept;
    const char* ToString(
        LightingTransportProductKind product) noexcept;
    bool SupportsProduct(
        const LightingTransportBackendCapabilities& capabilities,
        LightingTransportProductKind product) noexcept;

} // namespace HIKARI::RENDER3D::LIGHTING::TRANSPORT
