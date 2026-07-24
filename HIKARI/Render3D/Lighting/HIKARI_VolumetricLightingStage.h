#pragma once

#include <cstdint>

#include "Gfx/HIKARI_GfxContext.h"

namespace HIKARI {
    class RenderTarget2D;
    struct SceneEnvironment;
    enum class RenderDebugView : uint32_t;
}

namespace HIKARI::RENDER3D {
    enum class VolumetricLightingQuality : uint8_t;
}

namespace HIKARI::RENDER3D::TEMPORAL {
    struct TemporalFrameState;
}

namespace HIKARI::RENDER3D::VOLUMETRIC {

    struct VolumetricLightingStats {
        bool requested = false;
        bool active = false;
        bool historyValid = false;
        bool shadowed = false;
        uint32_t renderWidth = 0;
        uint32_t renderHeight = 0;
        uint32_t froxelWidth = 0;
        uint32_t froxelHeight = 0;
        uint32_t froxelDepth = 0;
        uint32_t froxelPixelSize = 0;
        uint32_t qualityLevel = 0;
        uint32_t pointLightCount = 0;
        uint32_t dispatchCount = 0;
        float temporalWeight = 0.0f;
        uint64_t workingSetBytes = 0;
        uint64_t resizeCount = 0;
        uint64_t historyResetCount = 0;
    };

    bool PrepareVolumetricLightingFrame(
        const GFX::Context& context,
        const TEMPORAL::TemporalFrameState& temporalFrame,
        const SceneEnvironment& environment,
        VolumetricLightingQuality quality,
        RenderDebugView debugView);

    RenderTarget2D* ExecuteVolumetricLightingStage(RenderTarget2D& sceneTarget);
    bool IsVolumetricLightingActive();
    bool IsVolumetricLightingDebugViewActive();
    const VolumetricLightingStats& GetVolumetricLightingStats();
    void ShutdownVolumetricLightingStage();

} // namespace HIKARI::RENDER3D::VOLUMETRIC
