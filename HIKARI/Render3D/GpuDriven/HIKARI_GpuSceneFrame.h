#pragma once

#include <cstdint>

namespace HIKARI::RENDER3D::GPUDRIVEN {

    class SurfaceGpuSceneFrameBuffer;

    struct GpuSceneFrame {
        SurfaceGpuSceneFrameBuffer* instanceBuffer = nullptr;
        uint32_t instanceCount = 0;
        uint32_t opaqueBaseIndex = 0;
        uint32_t depthPrepassBaseIndex = 0;
        uint32_t depthAwareBaseIndex = 0;
        uint32_t transparentBaseIndex = 0;
        uint32_t shadowBaseIndex = 0;
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
