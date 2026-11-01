#include "Render3D/Color/HIKARI_ViewExposure.h"

namespace HIKARI::RENDER3D::COLOR {
namespace {
constexpr MATH::Vec4 kAbsoluteExposure{1.0f, 1.0f, 1.0f, 1.0f};
MATH::Vec4 gActiveViewExposure = kAbsoluteExposure;
} // namespace

MATH::Vec4 MakeViewExposureConstants(const ExposureFrameState& state) noexcept {
    if (!state.valid || !(state.preExposure > 0.0f) || !(state.exposure > 0.0f)) {
        return kAbsoluteExposure;
    }
    const float preExposure = state.preExposure;
    return {preExposure, 1.0f / preExposure, preExposure / state.exposure, PreExposureRatio(state)};
}

ScopedViewExposure::ScopedViewExposure(const ExposureFrameState& state) noexcept : previous_(gActiveViewExposure) {
    gActiveViewExposure = MakeViewExposureConstants(state);
}

ScopedViewExposure::~ScopedViewExposure() { gActiveViewExposure = previous_; }

const MATH::Vec4& GetActiveViewExposure() noexcept { return gActiveViewExposure; }

} // namespace HIKARI::RENDER3D::COLOR
