#pragma once

#include <cstdint>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Lighting/HIKARI_LocalLightList.h"
#include "Render3D/Resources/HIKARI_RenderTextureView.h"

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

namespace HIKARI::RENDER3D::WATER {
    struct WaterVisibleDepthView;
}

namespace HIKARI::SHADOW { struct LocalLightShadowFrame; }

namespace HIKARI::RENDER3D::CLOUDS { struct CloudSolarVisibilityFrame; }

namespace HIKARI::RENDER3D::VOLUMETRIC {

    struct VolumetricLightingStats {
        bool requested = false;
        bool active = false;
        bool sceneTargetsReady = false;
        bool historyValid = false;
        bool shadowed = false;
        uint32_t localShadowFaces = 0;
        bool waterDepthValid = false;
        bool waterDepthFallback = true;
        uint32_t renderWidth = 0;
        uint32_t renderHeight = 0;
        uint32_t froxelWidth = 0;
        uint32_t froxelHeight = 0;
        uint32_t froxelDepth = 0;
        uint32_t froxelPixelSize = 0;
        uint32_t qualityLevel = 0;
        uint32_t localLightCount = 0;
        uint32_t pointLightCount = 0;
        uint32_t spotLightCount = 0;
        uint32_t dispatchCount = 0;
        float temporalWeight = 0.0f;
        uint64_t workingSetBytes = 0;
        uint64_t resizeCount = 0;
        uint64_t historyResetCount = 0;
    };

    bool PrepareVolumetricLightingFrame(
        const GFX::Context& context,
        const TEMPORAL::TemporalFrameState& temporalFrame,
        const RenderTextureView& sceneColor,
        const RenderTextureView& sceneDepth,
        const SceneEnvironment& environment,
        const LIGHTING::LocalLightListGpuBindings& localLights,
        VolumetricLightingQuality quality,
        RenderDebugView debugView,
        const SHADOW::LocalLightShadowFrame* localShadows = nullptr,
        const CLOUDS::CloudSolarVisibilityFrame* clouds = nullptr);

    RenderTarget2D* ExecuteVolumetricLightingStage(RenderTarget2D& sceneTarget);
    // Borrow a producer-owned water depth view for the prepared frame/view.
    // Prepare clears this input every frame; Execute falls back to opaque
    // SceneDepth when the token, dimensions, or frame do not match.
    void SetVolumetricVisibleWaterDepth(
        const WATER::WaterVisibleDepthView& view);
    void ClearVolumetricVisibleWaterDepth();
    bool IsVolumetricLightingActive();
    bool IsVolumetricLightingDebugViewActive();
    const VolumetricLightingStats& GetVolumetricLightingStats();
    void ShutdownVolumetricLightingStage();

} // namespace HIKARI::RENDER3D::VOLUMETRIC
