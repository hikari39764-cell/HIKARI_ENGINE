#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneSurfaceRecord.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    struct OwnedTraditionalIndirectStream {
        std::vector<GpuSceneSurfaceRecord> records{};
        std::vector<uint32_t> executableRecordIndices{};
        std::vector<RUNTIME::SurfaceDrawCommand> commands{};
        std::vector<RUNTIME::SurfaceGpuSceneInstance> instances{};
        std::vector<RUNTIME::SurfaceGpuSceneMaterialSource>
            materialSources{};
        std::vector<std::vector<MATH::Mat4>> jointPalettes{};
        std::vector<VFX::VariantKey> bucketVariants{};
        uint32_t gpuSceneBaseIndex = 0;
        uint32_t gpuSceneInstanceCount = 0;
        uint32_t staticCommandCount = 0;
        uint32_t skinnedCommandCount = 0;

        void Clear();
        bool CopyFrom(
            const GpuDrivenTraditionalIndirectView& view);
        bool CopyRangeFrom(
            const GpuDrivenTraditionalIndirectView& view,
            size_t firstCommand,
            size_t commandCount,
            bool skinnedRange);
        GpuDrivenTraditionalIndirectView GetView() const;
        void AttachTo(GpuDrivenPassSource& pass) const;
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
