#pragma once

#include "Render3D/Features/HIKARI_RenderFeatureTypes.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {
    struct GpuDrivenSceneSource;
}

namespace HIKARI::RENDER3D::FEATURES {

    RenderFeatureRequestSet AnalyzeRenderFeatureRequests(
        const GPUDRIVEN::GpuDrivenSceneSource& source);

} // namespace HIKARI::RENDER3D::FEATURES
