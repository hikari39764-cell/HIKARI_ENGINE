#pragma once

#include <d3d12.h>

#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"

namespace HIKARI::RENDER3D::TEMPORAL {

    bool ExecuteTemporalGeometryPass(
        const TemporalInputs& inputs,
        D3D12_CPU_DESCRIPTOR_HANDLE readOnlyDepthDsv);
    void ShutdownTemporalGeometryPass();

} // namespace HIKARI::RENDER3D::TEMPORAL
