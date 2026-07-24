#pragma once

#include "Editor/HIKARI_EditorContext.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI::EDITOR {

    class ReflectionProbeGizmoRenderer {
    public:
        void Submit(
            const SceneEnvironment& environment,
            const ViewportOverlayState& overlays,
            const Camera3D& camera) const;
    };

} // namespace HIKARI::EDITOR
