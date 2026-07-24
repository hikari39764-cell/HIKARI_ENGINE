#include "Render3D/Upscaling/HIKARI_StreamlineFrameGenerationPolicy.h"

#include <algorithm>

#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Upscaling/HIKARI_StreamlineFrameGeneration.h"

namespace HIKARI::RENDER3D::UPSCALING {

    void SynchronizeStreamlineFrameGenerationPolicy(
        const RenderQualitySettings& quality) {
        StreamlineFrameGenerationSettings settings{};
        settings.enabled =
            quality.frameGenerationMode == RenderFrameGenerationMode::Dlss;
        settings.generatedFrames = static_cast<uint32_t>(
            (std::max)(2, static_cast<int>(quality.frameGenerationMultiplier)) - 1);
        SetStreamlineFrameGenerationSettings(settings);
    }

} // namespace HIKARI::RENDER3D::UPSCALING
