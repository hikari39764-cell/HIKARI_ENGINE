#pragma once

#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"

namespace HIKARI::RENDER3D::TEMPORAL {

    bool ExecuteTemporalMaskPass(const TemporalInputs& inputs);
    void ShutdownTemporalMaskPass();

} // namespace HIKARI::RENDER3D::TEMPORAL
