#pragma once

#include "Render3D/Lighting/Transport/HIKARI_LightingTransportScene.h"

namespace HIKARI {
    class SkyManager;

    struct SceneDocument;
    struct SceneEnvironment;

    namespace RENDER3D::LIGHTING {
        struct SceneLightingSet;
    }

    namespace RENDER3D::RUNTIME {
        class SceneRenderCache;
    }

    namespace DOCUMENT_SCENE_BAKING {

        RENDER3D::LIGHTING::TRANSPORT::LightingTransportScene
            BuildLightingTransportScene(
                const SceneDocument& document,
                const SceneEnvironment& environment,
                const RENDER3D::LIGHTING::SceneLightingSet&
                    lighting,
                const RENDER3D::RUNTIME::SceneRenderCache&
                    renderCache,
                const SkyManager* skyManager = nullptr);

    } // namespace DOCUMENT_SCENE_BAKING

} // namespace HIKARI
