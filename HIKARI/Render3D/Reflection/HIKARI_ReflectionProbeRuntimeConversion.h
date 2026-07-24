#pragma once

#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"

namespace HIKARI::REFLECTION {

    inline RuntimeReflectionProbeInfluenceShape ToRuntimeInfluenceShape(
        ReflectionProbeInfluenceShape shape) noexcept {

        return shape == ReflectionProbeInfluenceShape::Box
            ? RuntimeReflectionProbeInfluenceShape::Box
            : RuntimeReflectionProbeInfluenceShape::Sphere;
    }

    inline RuntimeReflectionProbeProjectionShape ToRuntimeProjectionShape(
        ReflectionProbeProjectionShape shape) noexcept {

        return shape == ReflectionProbeProjectionShape::Box
            ? RuntimeReflectionProbeProjectionShape::Box
            : RuntimeReflectionProbeProjectionShape::Infinite;
    }

} // namespace HIKARI::REFLECTION
