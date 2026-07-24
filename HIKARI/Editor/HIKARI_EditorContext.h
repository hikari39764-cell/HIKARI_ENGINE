#pragma once

#include <cstdint>
#include <string>

#include "Editor/Selection/HIKARI_EditorSelection.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Runtime/HIKARI_RuntimeResourceRefreshService.h"
#include "Scene/Debug/HIKARI_ComponentGizmoRegistry.h"
#include "Scene/Debug/HIKARI_ViewportDebugState.h"

namespace HIKARI {

    enum class EditorTransformGizmoOperation {
        Translate,
        Rotate,
        Scale,
    };

    enum class EditorTransformGizmoMode {
        World,
        Local,
    };

    struct EditorTransformGizmoState {
        bool enabled = true;
        EditorTransformGizmoOperation operation = EditorTransformGizmoOperation::Translate;
        EditorTransformGizmoMode mode = EditorTransformGizmoMode::World;
        bool snapEnabled = false;
        MATH::Vec3 translateSnap{ 0.5f, 0.5f, 0.5f };
        float rotateSnapDeg = 15.0f;
        float scaleSnap = 0.1f;
    };

    struct ViewportWindowState {
        bool showGameView = true;
        bool showViewportHud = true;
    };

    struct AuthoringWindowState {
        bool showSceneWorkspace = true;
        bool showInspector = true;
        bool showProjectFeatures = false;
        bool showSceneSystems = false;
        bool openInputEditorRequested = false;
    };

    struct ResourceWindowState {
        bool showAssetBrowser = true;
        bool showEnvironment = true;
        bool showQuality = true;
    };

    struct RuntimeDebugWindowState {
        bool showDebugWorkspace = false;
        bool showDebugView = false;
        bool showPerformanceAudit = false;
        bool showValidationLab = false;
    };

    struct DebugWindowState {
        ViewportWindowState viewport{};
        AuthoringWindowState authoring{};
        ResourceWindowState resources{};
        RuntimeDebugWindowState runtime{};
    };

    struct EditorContext {
        DebugWindowState windows{};
        ComponentGizmoState gizmos{};
        EditorTransformGizmoState transformGizmo{};
        ViewportOverlayState overlays{};
        ViewportPerformanceState viewportPerformance{};
        ViewportDebugViewState viewportDebug{};
        EditorSelection selection{};

        std::string sceneNameEditBuffer{ "Untitled" };
        std::string saveAsNameBuffer{ "Untitled" };
        bool saveAsNameOverriddenByUser = false;
        std::string prefabNameBuffer{ "NewPrefab" };
        std::string componentAddStatusMessage{};
        bool componentAddStatusIsError = false;
        uint64_t nextSceneObjectId = 1;
        bool sceneDirty = false;
        RuntimeResourceRefreshReport lastRuntimeRefreshReport{};
    };

} // namespace HIKARI
