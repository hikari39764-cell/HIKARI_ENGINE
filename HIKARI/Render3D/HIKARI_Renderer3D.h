#pragma once

#include "Render3D/Core/HIKARI_Camera3D.h"

namespace HIKARI::RENDERER3D {

    void Reset();
    void RenderAll(const Camera3D& camera, float screenW, float screenH);

} // namespace HIKARI::RENDERER3D
