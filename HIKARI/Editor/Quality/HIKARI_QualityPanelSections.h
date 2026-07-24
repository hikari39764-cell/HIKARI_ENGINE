#pragma once

#include "Render3D/Diagnostics/HIKARI_EnvironmentDiagnostics.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI::EDITOR::QUALITY_PANEL {

#if defined(HIKARI_WITH_EDITOR)
bool DrawRenderSettings();
void DrawDirectionalShadow(SceneEnvironment &environment);
void DrawAmbientOcclusion(
    SceneEnvironment &environment,
    const RENDER3D::DIAGNOSTICS::EnvironmentDiagnosticsSnapshot
        &runtimeSnapshot);
void DrawBloom(SceneEnvironment &environment);
void DrawToneMapping(SceneEnvironment &environment);
void DrawGlobalPost(SceneEnvironment &environment);
void DrawTransitionDebug();
#endif

} // namespace HIKARI::EDITOR::QUALITY_PANEL
