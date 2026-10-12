#pragma once

#include <cstdint>
#include <memory>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Color/HIKARI_ExposureState.h"

namespace HIKARI {
class RenderTarget2D;
}

namespace HIKARI::RENDER3D::COLOR {

struct AutomaticExposureDebugState {
    bool initialized = false;
    bool measurementQueued = false;
    bool measurementValid = false;
    float meteredLuminance = 0.18f;
    float currentExposure = 1.0f;
    float targetExposure = 1.0f;
    uint64_t measuredFrameIndex = 0;
};

class AutomaticExposureMeter final {
public:
    AutomaticExposureMeter();
    ~AutomaticExposureMeter();

    AutomaticExposureMeter(const AutomaticExposureMeter&) = delete;
    AutomaticExposureMeter& operator=(const AutomaticExposureMeter&) = delete;

    void UpdateContext(const GFX::Context& context);
    void Shutdown();

    ExposureFrameState BeginFrame(const ExposureSettings& settings, const ExposureIllumination& illumination,
                                  float deltaTime);
    // sceneColor はこのフレームの P で Pre-Exposure 済み。測光は絶対輝度に戻して行う。
    bool QueueMeasurement(RenderTarget2D& sceneColor, float preExposure, uint64_t frameIndex);

    const AutomaticExposureDebugState& GetDebugState() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace HIKARI::RENDER3D::COLOR
