#include "Render3D/HIKARI_Renderer3D.h"

#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"

namespace HIKARI::RENDERER3D {

    void Reset() {
        DEBUG::Reset();
    }

    void RenderAll(const Camera3D& camera, float screenW, float screenH) {
        DEBUG::RenderAll(camera, screenW, screenH);
    }

} // namespace HIKARI::RENDERER3D
