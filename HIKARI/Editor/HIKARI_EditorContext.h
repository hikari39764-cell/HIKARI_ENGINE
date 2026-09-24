#pragma once

#include <cstdint>
#include <string>

#include "Editor/Selection/HIKARI_EditorSelection.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Runtime/HIKARI_RuntimeResourceRefreshService.h"
#include "Scene/Debug/HIKARI_ComponentGizmoRegistry.h"
#include "Scene/Debug/HIKARI_ViewportDebugState.h"
#include "Scene/Terrain/HIKARI_TerrainProcedural.h"

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

enum class EditorTerrainToolMode : uint8_t {
    Raise,
    Lower,
    Smooth,
    Flatten,
    Mound,
    Pit,
    Ridge,
    Terrace,
    Shaft,
    Hole,
    CliffOpening,
    Ramp,
    Noise,
};

enum class EditorTerrainPaintMode : uint8_t {
    None,
    Sand,
    Snow,
    Dirt,
    Grass,
    Rock,
    Erase,
};

enum class EditorTerrainWorkspacePage : uint8_t {
    Sculpt,
    Paint,
    Foliage,
    Place,
    Generate,
};

struct EditorTransformGizmoState {
    bool enabled = true;
    EditorTransformGizmoOperation operation = EditorTransformGizmoOperation::Translate;
    EditorTransformGizmoMode mode = EditorTransformGizmoMode::World;
    bool snapEnabled = false;
    MATH::Vec3 translateSnap{0.5f, 0.5f, 0.5f};
    float rotateSnapDeg = 15.0f;
    float scaleSnap = 0.1f;
};

// 地形工作区のセッション状態。ドキュメントには保存しない。
struct EditorTerrainToolState {
    // Terrain target is independent from the scene selection. This keeps
    // sculpting stable and lets Place operate on selected props without
    // losing the active terrain.
    SceneObjectId targetObjectId{};
    EditorTerrainWorkspacePage page = EditorTerrainWorkspacePage::Sculpt;
    EditorTerrainToolMode mode = EditorTerrainToolMode::Raise;
    EditorTerrainPaintMode paint = EditorTerrainPaintMode::None;
    bool active = false;
    bool hasHit = false;
    MATH::Vec3 hitPoint{};
    bool sampleHeightRequested = false;
    bool sampledHeightReady = false;
    float sampledHeight = 0.0f;
    bool rampAnchorSet = false;
    MATH::Vec3 rampStartLocal{};
    float rampHalfWidth = 2.0f;
    float rampShoulderWidth = 1.0f;
    float rampBlendAmount = 1.0f;
    float openingWidth = 3.0f;
    float openingHeight = 3.0f;
    float openingDepth = 2.0f;
    float openingFeather = 0.20f;
    float placementOffset = 0.0f;
    TERRAIN::TerrainProceduralSpec procedural{};
    TERRAIN::TerrainProceduralPreview proceduralPreview{};
    bool proceduralPreviewRequested = false;
    SceneObjectId proceduralPreviewObjectId{};
    uint64_t proceduralPreviewTerrainRevision = 0u;
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
    bool showDebugView = false;
    bool showPerformanceAudit = false;
    bool showAiAssistant = false;
    bool showEditorSettings = false;
};

struct DebugWindowState {
    ViewportWindowState viewport{};
    AuthoringWindowState authoring{};
    ResourceWindowState resources{};
    RuntimeDebugWindowState runtime{};
};

struct EditorAiIntegrationSettings {
    bool localBridgeEnabled = false;
};

struct EditorContext {
    DebugWindowState windows{};
    EditorAiIntegrationSettings aiIntegration{};
    ComponentGizmoState gizmos{};
    EditorTransformGizmoState transformGizmo{};
    EditorTerrainToolState terrainTool{};
    ViewportOverlayState overlays{};
    ViewportPerformanceState viewportPerformance{};
    ViewportDebugViewState viewportDebug{};
    EditorSelection selection{};

    std::string sceneNameEditBuffer{"Untitled"};
    std::string saveAsNameBuffer{"Untitled"};
    bool saveAsNameOverriddenByUser = false;
    std::string prefabNameBuffer{"NewPrefab"};
    std::string componentAddStatusMessage{};
    bool componentAddStatusIsError = false;
    uint64_t nextSceneObjectId = 1;
    bool sceneDirty = false;
    RuntimeResourceRefreshReport lastRuntimeRefreshReport{};
};

} // namespace HIKARI
