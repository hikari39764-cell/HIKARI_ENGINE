#include "Render3D/Lighting/Transport/HIKARI_LightingTransportContract.h"

namespace HIKARI::RENDER3D::LIGHTING::TRANSPORT {

    const char* ToString(
        LightingTransportBackendKind backend) noexcept {

        switch (backend) {
        case LightingTransportBackendKind::RasterCapture:
            return "RasterCapture";
        case LightingTransportBackendKind::DxrPathTracing:
            return "DxrPathTracing";
        default:
            return "Unknown";
        }
    }

    const char* ToString(
        LightingTransportProductKind product) noexcept {

        switch (product) {
        case LightingTransportProductKind::SpecularReflection:
            return "SpecularReflection";
        case LightingTransportProductKind::DiffuseIrradiance:
            return "DiffuseIrradiance";
        case LightingTransportProductKind::SurfaceLightmap:
            return "SurfaceLightmap";
        default:
            return "Unknown";
        }
    }

    bool SupportsProduct(
        const LightingTransportBackendCapabilities& capabilities,
        LightingTransportProductKind product) noexcept {

        if (!capabilities.available) {
            return false;
        }
        switch (product) {
        case LightingTransportProductKind::SpecularReflection:
            return capabilities.supportsSpecularReflection;
        case LightingTransportProductKind::DiffuseIrradiance:
            return capabilities.supportsDiffuseIrradiance;
        case LightingTransportProductKind::SurfaceLightmap:
            return capabilities.supportsSurfaceLightmaps;
        default:
            return false;
        }
    }

} // namespace HIKARI::RENDER3D::LIGHTING::TRANSPORT
