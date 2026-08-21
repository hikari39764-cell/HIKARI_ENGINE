#pragma once

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_SkyManager.h"

namespace HIKARI::RENDER3D::PANORAMA_SKY {

    // Composes the optional alpha-bearing equirectangular foreground after the
    // base sky.  This is deliberately separate from both SkyRenderer and the
    // volumetric stylized-cloud owner.
    bool RenderPanoramaSkyLayer(
        const GFX::Context& graphics,
        const Camera3D& camera,
        const SkySettings& settings,
        SkyManager& skyManager);

    void InvalidatePanoramaSkyLayerTextureCache();
    void ShutdownPanoramaSkyLayerStage();

} // namespace HIKARI::RENDER3D::PANORAMA_SKY
