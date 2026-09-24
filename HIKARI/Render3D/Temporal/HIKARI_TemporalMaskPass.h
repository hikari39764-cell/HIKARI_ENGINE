#pragma once

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"

namespace HIKARI::RENDER3D::TEMPORAL {

bool ExecuteTemporalMaskPass(const GFX::Context& graphics, const TemporalInputs& inputs);
void ShutdownTemporalMaskPass();

} // namespace HIKARI::RENDER3D::TEMPORAL
