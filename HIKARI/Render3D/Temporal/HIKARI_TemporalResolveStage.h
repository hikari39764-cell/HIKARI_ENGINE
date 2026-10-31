#pragma once

#include <cstdint>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/ScreenSpace/HIKARI_ScreenReadability.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"

namespace HIKARI {
class RenderTarget2D;
}

namespace HIKARI::RENDER3D {
struct RenderQualitySettings;
}

namespace HIKARI::RENDER3D::COLOR {
struct ExposureFrameState;
}

namespace HIKARI::RENDER3D::TEMPORAL {

enum class TemporalResolveBackend : uint8_t {
    Off,
    Taa,
    Streamline,
    TaaFallback,
};

struct TemporalResolveStageResult {
    RenderTarget2D* output = nullptr;
    TemporalResolveBackend backend = TemporalResolveBackend::Off;
    uint32_t expectedOutputWidth = 0;
    uint32_t expectedOutputHeight = 0;
    bool requested = false;
    bool resolved = false;
    bool requiresOutputNormalization = false;
    // Output raster phase in render pixels. Native TAA and DLSS both resolve to unjittered pixel centres.
    MATH::Vec2 outputJitterPixels{};
    // 後処理の線が読む、出力の解像度で時間方向に均した深度（R32_FLOAT）。無い時は元の深度を読む。
    RenderTarget2D* stableDepth = nullptr;
    // その SRV（共有の記述子ヒープ上）。画面の貼り直しが読む。
    D3D12_GPU_DESCRIPTOR_HANDLE stableDepthSrv{};
};

TemporalResolveStageResult ExecuteTemporalResolveStage(
    TemporalViewLease view, RenderFrameToken frame, TemporalResourceFrame resources, const GFX::Context& graphics,
    RenderTarget2D& sceneColorTarget, RenderTarget2D& sceneDepthTarget, const RenderQualitySettings& quality,
    const COLOR::ExposureFrameState& exposureState, const ScreenReadabilityFrame& sceneCaptureSurfaces = {},
    RenderTarget2D* maskSceneColor = nullptr);

const char* ToString(TemporalResolveBackend backend);

} // namespace HIKARI::RENDER3D::TEMPORAL
