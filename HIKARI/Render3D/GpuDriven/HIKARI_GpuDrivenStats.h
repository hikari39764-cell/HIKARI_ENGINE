#pragma once

#include <cstddef>

namespace HIKARI::RENDER3D::GPUDRIVEN {

    struct GpuDrivenStats {
        size_t sourceInstanceCount = 0;
        size_t residentInstanceCount = 0;
        size_t visibilitySeedCount = 0;
        size_t visibilityOverflowInstanceCount = 0;
        bool sceneResident = false;
        bool visibilityReady = false;
        bool commandBuildReady = false;
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
