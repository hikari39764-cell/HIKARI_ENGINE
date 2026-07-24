#pragma once

#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"

namespace HIKARI {
    class RenderTarget2D;
}

namespace HIKARI::RENDER3D::TEMPORAL {

    struct TaaResolveSettings {
        bool enabled = true;
        float historyWeight = 0.90f;
        float varianceClipGamma = 1.0f;
        float depthRejection = 0.0025f;
        float luminanceRejection = 0.55f;
        float sharpness = 0.25f;
    };

    RenderTarget2D* ExecuteTaaResolvePass(
        const TemporalInputs& inputs,
        const TaaResolveSettings& settings);
    void ShutdownTaaResolvePass();

} // namespace HIKARI::RENDER3D::TEMPORAL
