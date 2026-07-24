#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenFrame.h"
#include "Render3D/Runtime/HIKARI_SurfaceGpuScene.h"
#include "Vfx/Common/HIKARI_FxTypes.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    struct GpuSceneDirtyRange {
        uint32_t firstInstance = 0;
        uint32_t instanceCount = 0;

        bool IsValid() const {
            return instanceCount != 0u;
        }
    };

    struct GpuSceneSurfaceRecord;

    struct GpuDrivenTraditionalIndirectView {
        const std::vector<GpuSceneSurfaceRecord>* records = nullptr;
        const std::vector<uint32_t>* executableRecordIndices = nullptr;
        const std::vector<RUNTIME::SurfaceDrawCommand>* commands = nullptr;
        const std::vector<RUNTIME::SurfaceGpuSceneInstance>* instances = nullptr;
        const std::vector<RUNTIME::SurfaceGpuSceneMaterialSource>* materialSources = nullptr;
        const std::vector<std::vector<MATH::Mat4>>* jointPalettes = nullptr;
        const std::vector<VFX::VariantKey>* bucketVariants = nullptr;
        uint32_t gpuSceneBaseIndex = 0;
        uint32_t gpuSceneInstanceCount = 0;
        uint32_t staticCommandCount = 0;
        uint32_t skinnedCommandCount = 0;

        void Reset();
        bool HasCommands() const;
        bool HasGpuSceneRange() const;
        bool HasGpuSceneInstances() const;
        size_t CommandCount() const;
    };

    struct GpuDrivenPassSource {
        const std::vector<RUNTIME::SurfaceGpuSceneInstance>* instances = nullptr;
        const std::vector<RUNTIME::SurfaceGpuSceneMaterialSource>* materialSources = nullptr;
        // GPU scene の常駐範囲。CPU 側 view は upload/material patch 用の補助に留める。
        uint32_t gpuSceneBaseIndex = 0;
        uint32_t gpuSceneInstanceCount = 0;
        GpuDrivenBackendKind preferredBackend = GpuDrivenBackendKind::TraditionalIndirect;
        bool clusterEligible = false;
        GpuDrivenTraditionalIndirectView traditionalIndirect{};
        std::vector<GpuSceneDirtyRange> dirtyRanges{};

        void Reset();
        bool HasPrimaryGpuSceneRange() const;
        bool HasPrimaryGpuSceneInstances() const;
        bool HasGpuSceneRange() const;
        bool HasGpuSceneInstances() const;
        bool HasDirtyGpuSceneRanges() const;
        GpuDrivenPassBuildInput ToFrameBuildInput() const;
    };

    struct GpuDrivenSceneSource {
        std::array<GpuDrivenPassSource, kGpuDrivenPassCount> passes{};
        const std::vector<std::vector<MATH::Mat4>>* meshShaderJointPalettes = nullptr;
        uint64_t layoutVersion = 0;
        uint64_t sourceVersion = 0;
        uint64_t dirtyBaseSourceVersion = 0;
        size_t sourceInstanceCount = 0;
        uint32_t sourceRecordCount = 0;

        void Reset();
        GpuDrivenPassSource& GetPass(GpuDrivenPassKind passKind);
        const GpuDrivenPassSource& GetPass(GpuDrivenPassKind passKind) const;
        bool HasAnyGpuSceneRanges() const;
        bool HasAnyGpuSceneInstances() const;
        bool HasAnyDirtyGpuSceneRanges() const;
        size_t CountGpuSceneInstances() const;
    };

    GpuDrivenFrameBuildInput BuildGpuDrivenFrameInput(
        const GpuDrivenSceneSource& source);

} // namespace HIKARI::RENDER3D::GPUDRIVEN
