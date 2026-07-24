#pragma once

#include <array>
#include <cstddef>

#include "Render3D/GpuDriven/Backend/HIKARI_GeometryBackendContext.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    constexpr size_t kMaxGeometryBackendPlanBackends = 2;

    struct GeometryBackendPolicy {
        GeometryBackendKind preferred = GeometryBackendKind::GpuDrivenMeshShader;
        GeometryBackendKind secondary = GeometryBackendKind::GpuDrivenTraditionalVsPs;
        bool forcePreferredOnly = false;
    };

    struct GeometryBackendExecutionPlan {
        std::array<GeometryBackendKind, kMaxGeometryBackendPlanBackends> gpuBackends{};
        size_t gpuBackendCount = 0;

        bool AddGpuBackend(GeometryBackendKind backend);
    };

    GeometryBackendPolicy ResolveGeometryBackendPolicy(GpuDrivenPassKind pass);
    GeometryBackendExecutionPlan BuildGeometryBackendExecutionPlan(
        const GeometryBackendPolicy& policy);
    GeometryBackendExecutionPlan ResolveGeometryBackendExecutionPlan(
        GpuDrivenPassKind pass);

} // namespace HIKARI::RENDER3D::GPUDRIVEN
