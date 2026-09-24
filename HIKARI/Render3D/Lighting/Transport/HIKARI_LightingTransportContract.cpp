#include "Render3D/Lighting/Transport/HIKARI_LightingTransportContract.h"

namespace HIKARI::RENDER3D::LIGHTING::TRANSPORT {

const char* ToString(LightingTransportBackendKind backend) noexcept {
    switch (backend) {
    case LightingTransportBackendKind::RasterCapture: return "RasterCapture";
    case LightingTransportBackendKind::DxrPathTracing: return "DxrPathTracing";
    default: return "Unknown";
    }
}

const char* ToString(LightingTransportProductKind product) noexcept {
    switch (product) {
    case LightingTransportProductKind::SpecularReflection: return "SpecularReflection";
    case LightingTransportProductKind::DiffuseIrradiance: return "DiffuseIrradiance";
    case LightingTransportProductKind::SurfaceLightmap: return "SurfaceLightmap";
    default: return "Unknown";
    }
}

} // namespace HIKARI::RENDER3D::LIGHTING::TRANSPORT
