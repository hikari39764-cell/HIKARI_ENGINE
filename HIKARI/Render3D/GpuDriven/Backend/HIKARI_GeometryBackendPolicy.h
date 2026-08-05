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

    constexpr bool IsGeometryFeatureBackendReady(
        const GeometryBackendPolicy& policy,
        bool meshShaderReady,
        bool traditionalVsPsReady) noexcept {

        const auto isReady = [meshShaderReady, traditionalVsPsReady](
            GeometryBackendKind backend) constexpr noexcept {
            switch (backend) {
            case GeometryBackendKind::GpuDrivenMeshShader:
                return meshShaderReady;
            case GeometryBackendKind::GpuDrivenTraditionalVsPs:
                return traditionalVsPsReady;
            default:
                return false;
            }
        };

        if (isReady(policy.preferred)) {
            return true;
        }
        return !policy.forcePreferredOnly && isReady(policy.secondary);
    }

    GeometryBackendPolicy ResolveGeometryBackendPolicy(GpuDrivenPassKind pass);
    GeometryBackendExecutionPlan BuildGeometryBackendExecutionPlan(
        const GeometryBackendPolicy& policy);
    GeometryBackendExecutionPlan ResolveGeometryBackendExecutionPlan(
        GpuDrivenPassKind pass);

} // namespace HIKARI::RENDER3D::GPUDRIVEN
