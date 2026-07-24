#pragma once

#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI {

    inline const char* GetSsaoModeName(SsaoMode mode) noexcept {
        switch (mode) {
        case SsaoMode::Reference:
            return "Reference";
        case SsaoMode::OptimizedHigh:
            return "OptimizedHigh";
        case SsaoMode::Balanced:
            return "Balanced";
        case SsaoMode::Off:
        default:
            return "Off";
        }
    }

} // namespace HIKARI
