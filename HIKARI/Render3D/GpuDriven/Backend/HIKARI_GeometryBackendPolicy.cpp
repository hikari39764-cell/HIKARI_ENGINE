#include "Render3D/GpuDriven/Backend/HIKARI_GeometryBackendPolicy.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    namespace {

        bool IsKnownGpuBackend(GeometryBackendKind backend) {
            switch (backend) {
            case GeometryBackendKind::GpuDrivenTraditionalVsPs:
            case GeometryBackendKind::GpuDrivenMeshShader:
                return true;
            default:
                return false;
            }
        }

        GeometryBackendPolicy MakePolicyFromQualitySettings() {
            GeometryBackendPolicy policy{};
            policy.preferred = GeometryBackendKind::GpuDrivenMeshShader;
            policy.secondary = GeometryBackendKind::GpuDrivenTraditionalVsPs;
            policy.forcePreferredOnly = true;

            switch (RENDER3D::GetRenderQualitySettings().geometryPipeline) {
            case RENDER3D::GeometryPipelineMode::TraditionalVsPs:
                policy.preferred = GeometryBackendKind::GpuDrivenTraditionalVsPs;
                policy.secondary = GeometryBackendKind::GpuDrivenMeshShader;
                policy.forcePreferredOnly = true;
                break;
            case RENDER3D::GeometryPipelineMode::AutoFallback:
                policy.preferred = GeometryBackendKind::GpuDrivenMeshShader;
                policy.secondary = GeometryBackendKind::GpuDrivenTraditionalVsPs;
                policy.forcePreferredOnly = false;
                break;
            case RENDER3D::GeometryPipelineMode::MeshShader:
            default:
                break;
            }

            return policy;
        }

    } // namespace

    bool GeometryBackendExecutionPlan::AddGpuBackend(
        GeometryBackendKind backend) {

        if (!IsKnownGpuBackend(backend)) {
            return false;
        }
        for (size_t i = 0; i < gpuBackendCount; ++i) {
            if (gpuBackends[i] == backend) {
                return true;
            }
        }
        if (gpuBackendCount >= gpuBackends.size()) {
            return false;
        }
        gpuBackends[gpuBackendCount++] = backend;
        return true;
    }

    GeometryBackendPolicy ResolveGeometryBackendPolicy(GpuDrivenPassKind pass) {
        GeometryBackendPolicy policy = MakePolicyFromQualitySettings();

        switch (pass) {
        case GpuDrivenPassKind::ForwardOpaque:
        case GpuDrivenPassKind::GeometryAux:
        case GpuDrivenPassKind::DepthPrepass:
        case GpuDrivenPassKind::Shadow:
        case GpuDrivenPassKind::ReflectionCapture:
        case GpuDrivenPassKind::DepthAware:
        case GpuDrivenPassKind::Transparent:
        case GpuDrivenPassKind::Debug:
        default:
            return policy;
        }
    }

    GeometryBackendExecutionPlan BuildGeometryBackendExecutionPlan(
        const GeometryBackendPolicy& policy) {

        GeometryBackendExecutionPlan plan{};
        plan.AddGpuBackend(policy.preferred);
        if (!policy.forcePreferredOnly) {
            plan.AddGpuBackend(policy.secondary);
        }
        return plan;
    }

    GeometryBackendExecutionPlan ResolveGeometryBackendExecutionPlan(
        GpuDrivenPassKind pass) {

        return BuildGeometryBackendExecutionPlan(
            ResolveGeometryBackendPolicy(pass));
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
