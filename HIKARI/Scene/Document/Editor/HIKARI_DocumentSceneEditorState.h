#pragma once

#include <cstdint>
#include <string>

#include "Assets/HIKARI_AssetGuid.h"
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
        std::string sceneIdSnapshot{};
        std::string scenePathSnapshot{};
        AssetGuid sceneAssetGuidSnapshot{};
        uint64_t documentRevisionSnapshot = 0;
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
