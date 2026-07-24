#pragma once
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"

namespace HIKARI::RENDERER3D {

    // Facade for current 3D rendering entry point.
    // Current implementation forwards to Debug Primitive Renderer.
    // Future Mesh/Material/Model renderer should be implemented behind this facade.
    using WireCube = DEBUG::WireCube;

    void Reset();
    void SubmitWireCube(const WireCube& cube);
    void RenderAll(const Camera3D& camera, float screenW, float screenH);

} // namespace HIKARI::RENDERER3D
