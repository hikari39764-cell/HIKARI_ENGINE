#pragma once

#include "Editor/HIKARI_EditorContext.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::EDITOR {

    class LightProbeVolumeGizmoRenderer {
    public:
        void Submit(
            const LightProbeVolumeSettings& settings,
            const ViewportOverlayState& overlays) const;
    };

} // namespace HIKARI::EDITOR
