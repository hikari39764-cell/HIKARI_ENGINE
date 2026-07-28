#pragma once

#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Scene/Debug/HIKARI_ComponentGizmoRenderer.h"
#include "Scene/Debug/HIKARI_ViewportDebugState.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

    struct DocumentSceneEditorState {
        ComponentGizmoState componentGizmoSnapshot{};
        ViewportOverlayState overlaySnapshot{};
        ViewportPerformanceState performanceSnapshot{};
        ViewportDebugViewState debugViewSnapshot{};
        SceneObjectId selectedGizmoObjectSnapshot{};
        SceneDocument documentSnapshot{};
        SceneEnvironment environmentSnapshot{};
        ComponentGizmoRenderer componentGizmoRenderer{};
        ComponentGizmoState componentGizmoState{};
        ViewportOverlayState viewportOverlayState{};
        ViewportPerformanceState viewportPerformanceState{};
        ViewportDebugViewState viewportDebugViewState{};
        SceneObjectId selectedGizmoObjectId{};
    };

} // namespace HIKARI
