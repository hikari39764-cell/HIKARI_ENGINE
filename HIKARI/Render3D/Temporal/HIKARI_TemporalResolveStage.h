#pragma once

#include <cstdint>

namespace HIKARI {
    class RenderTarget2D;
}

namespace HIKARI::RENDER3D {
    struct RenderQualitySettings;
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
    };

    TemporalResolveStageResult ExecuteTemporalResolveStage(
        RenderTarget2D& sceneColorTarget,
        RenderTarget2D& sceneDepthTarget,
        const RenderQualitySettings& quality,
        float exposure);

    const char* ToString(TemporalResolveBackend backend);

} // namespace HIKARI::RENDER3D::TEMPORAL
