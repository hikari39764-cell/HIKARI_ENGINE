#pragma once

#include <array>
#include <cstddef>

#include "Render3D/GpuDriven/Backend/HIKARI_GeometryBackendContext.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

// 品質設定の geometryPipeline から決まる主／副バックエンド。全 pass 共通。
struct GeometryBackendPolicy {
    GeometryBackendKind preferred = GeometryBackendKind::GpuDrivenMeshShader;
    GeometryBackendKind secondary = GeometryBackendKind::GpuDrivenTraditionalVsPs;
    bool forcePreferredOnly = false;
};

// 1 pass で実行するバックエンド（重複なし、最大 2 つ）
struct GeometryBackendExecutionPlan {
    std::array<GeometryBackendKind, 2> gpuBackends{};
    size_t gpuBackendCount = 0;

    void AddGpuBackend(GeometryBackendKind backend) {
        for (size_t i = 0; i < gpuBackendCount; ++i) {
            if (gpuBackends[i] == backend) {
                return;
            }
        }
        if (gpuBackendCount < gpuBackends.size()) {
            gpuBackends[gpuBackendCount++] = backend;
        }
    }
};

constexpr bool IsGeometryFeatureBackendReady(const GeometryBackendPolicy& policy, bool meshShaderReady,
                                             bool traditionalVsPsReady) noexcept {
    const auto isReady = [&](GeometryBackendKind backend) {
        return backend == GeometryBackendKind::GpuDrivenMeshShader ? meshShaderReady : traditionalVsPsReady;
    };
    return isReady(policy.preferred) || (!policy.forcePreferredOnly && isReady(policy.secondary));
}

GeometryBackendPolicy ResolveGeometryBackendPolicy();

} // namespace HIKARI::RENDER3D::GPUDRIVEN
