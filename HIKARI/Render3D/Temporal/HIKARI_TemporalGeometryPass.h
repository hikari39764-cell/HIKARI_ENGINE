#pragma once

#include <d3d12.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"

namespace HIKARI::RENDER3D::TEMPORAL {

    bool ExecuteTemporalGeometryPass(
        const GFX::Context& graphics,
        const TemporalInputs& inputs,
        D3D12_CPU_DESCRIPTOR_HANDLE readOnlyDepthDsv);
    void ShutdownTemporalGeometryPass();

} // namespace HIKARI::RENDER3D::TEMPORAL
