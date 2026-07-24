#pragma once

namespace HIKARI::RENDER3D {

    enum class RenderPhase {
        Opaque = 0,
        DepthAware,
        Transparent,
    };

} // namespace HIKARI::RENDER3D
