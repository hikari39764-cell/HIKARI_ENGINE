#pragma once

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {

    struct SceneEnvironment;

    namespace LIGHTDEBUGDRAW {

        void SubmitDirectionalLightArrow(const MATH::Vec3& directionalDir, const SceneEnvironment& environment);
        void SubmitPointLightDebug(const SceneEnvironment& environment);

    } // namespace LIGHTDEBUGDRAW

} // namespace HIKARI
