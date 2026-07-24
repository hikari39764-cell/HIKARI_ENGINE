#pragma once

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Procedural/HIKARI_ProceduralMeshTypes.h"

namespace HIKARI {
    class Camera3D;
}

namespace HIKARI::EDITOR {

    float EstimatePrimitivePlacementRadius(
        const ProceduralMeshSettings& settings) noexcept;

    MATH::Vec3 ComputeObjectPlacementInView(
        const Camera3D& camera,
        float boundingRadius = 0.5f) noexcept;

    MATH::Vec3 ComputePrimitivePlacementInView(
        const Camera3D& camera,
        const ProceduralMeshSettings& settings) noexcept;

} // namespace HIKARI::EDITOR
