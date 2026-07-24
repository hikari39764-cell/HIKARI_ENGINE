#include "Render3D/HIKARI_Renderer3D.h"

namespace HIKARI::RENDERER3D {

    void Reset() {
        DEBUG::Reset();
    }

    void SubmitWireCube(const WireCube& cube) {
        DEBUG::SubmitWireCube(cube);
    }

    void RenderAll(const Camera3D& camera, float screenW, float screenH) {
        DEBUG::RenderAll(camera, screenW, screenH);
    }

} // namespace HIKARI::RENDERER3D
