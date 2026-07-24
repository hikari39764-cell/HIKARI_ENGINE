#pragma once

namespace HIKARI::RENDER3D {
    struct RenderQualitySettings;
}

namespace HIKARI::RENDER3D::UPSCALING {

    void SynchronizeStreamlineFrameGenerationPolicy(
        const RenderQualitySettings& quality);

} // namespace HIKARI::RENDER3D::UPSCALING
