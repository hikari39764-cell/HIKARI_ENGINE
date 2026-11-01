#include "Render3D/GpuDriven/Backend/HIKARI_GeometryBackendPolicy.h"

#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

GeometryBackendPolicy ResolveGeometryBackendPolicy() {
    switch (RENDER3D::GetRenderQualitySettings().geometryPipeline) {
    case RENDER3D::GeometryPipelineMode::TraditionalVsPs:
        return {GeometryBackendKind::GpuDrivenTraditionalVsPs, GeometryBackendKind::GpuDrivenMeshShader, true};
    case RENDER3D::GeometryPipelineMode::AutoFallback:
        return {GeometryBackendKind::GpuDrivenMeshShader, GeometryBackendKind::GpuDrivenTraditionalVsPs, false};
    case RENDER3D::GeometryPipelineMode::MeshShader:
    default: return {GeometryBackendKind::GpuDrivenMeshShader, GeometryBackendKind::GpuDrivenTraditionalVsPs, true};
    }
}

} // namespace HIKARI::RENDER3D::GPUDRIVEN
