#pragma once

#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"

namespace HIKARI::RENDER3D::TEMPORAL {

    bool ExecuteMotionVectorPass(const TemporalInputs& inputs);
    void ShutdownMotionVectorPass();

} // namespace HIKARI::RENDER3D::TEMPORAL
