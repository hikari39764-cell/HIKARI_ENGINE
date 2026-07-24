#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Render3D/GpuDriven/HIKARI_GpuDrivenPass.h"
#include "Render3D/Runtime/HIKARI_SurfaceGpuScene.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    enum class GpuDrivenBackendKind : uint32_t {
        MeshShader,
        TraditionalIndirect,
    };

    struct GpuSceneRange {
        uint32_t baseIndex = 0;
        uint32_t instanceCount = 0;
        GpuDrivenPassKind passKind = GpuDrivenPassKind::ForwardOpaque;

        bool IsValid() const;
    };

    struct GpuDrivenPassFrame {
        const std::vector<RUNTIME::SurfaceGpuSceneInstance>* instances = nullptr;
        GpuSceneRange gpuSceneRange{};
        GpuDrivenBackendKind preferredBackend = GpuDrivenBackendKind::MeshShader;
        bool clusterEligible = false;

        void Reset();
        bool HasSource() const;
    };

    struct GpuDrivenFrame {
        std::array<GpuDrivenPassFrame, kGpuDrivenPassCount> passes{};

        void Reset();
        GpuDrivenPassFrame& GetPass(GpuDrivenPassKind passKind);
        const GpuDrivenPassFrame& GetPass(GpuDrivenPassKind passKind) const;
        size_t CountActivePasses() const;
        size_t CountClusterEligiblePasses() const;
        size_t CountSourceInstances() const;
        size_t CountClusterEligibleInstances() const;
    };

    struct GpuDrivenPassBuildInput {
        const std::vector<RUNTIME::SurfaceGpuSceneInstance>* instances = nullptr;
        uint32_t gpuSceneBaseIndex = 0;
        uint32_t gpuSceneInstanceCount = 0;
        GpuDrivenBackendKind preferredBackend = GpuDrivenBackendKind::MeshShader;
        bool clusterEligible = false;
    };

    struct GpuDrivenFrameBuildInput {
        std::array<GpuDrivenPassBuildInput, kGpuDrivenPassCount> passes{};
    };

    GpuDrivenFrame BuildGpuDrivenFrame(const GpuDrivenFrameBuildInput& input);

} // namespace HIKARI::RENDER3D::GPUDRIVEN
