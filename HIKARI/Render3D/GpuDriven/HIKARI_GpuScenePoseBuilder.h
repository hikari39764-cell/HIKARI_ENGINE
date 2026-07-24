#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Render3D/GpuDriven/HIKARI_GpuSceneSurfaceRecord.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    class GpuScenePoseBuilder final {
    public:
        const std::vector<MATH::Mat4>* ResolveJointPalette(
            const GpuSceneSurfaceRecord& record);

    private:
        struct PoseEntry {
            RUNTIME::SceneRenderObjectId objectId{};
            uint64_t objectVersion = 0;
            const ModelAsset* model = nullptr;
            std::vector<MATH::Mat4> localNodeGlobals{};
            std::vector<uint8_t> visited{};
            std::unordered_map<int, std::vector<MATH::Mat4>> palettesBySkin{};
        };

        PoseEntry* ResolvePoseEntry(const GpuSceneSurfaceRecord& record);

        std::vector<PoseEntry> entries_{};
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
