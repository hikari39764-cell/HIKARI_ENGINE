#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Editor/Selection/HIKARI_EditorSelection.h"
#include "Core/Math/HIKARI_Math3D.h"
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
    // 移動の手柄で動かす物を真下の地面へ載せたまま動かす（Unreal の表面吸着）。
    bool groundSnap = false;
    MATH::Vec3 translateSnap{0.5f, 0.5f, 0.5f};
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
    bool showDebugView = false;
    bool showPerformanceAudit = false;
    // 帧率と CPU / GPU のフレーム時間を画面の右上に出す（view.stats、新窗口 Play の窓にも出す）。
    bool showFrameStats = false;
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
    // 既定で開く。127.0.0.1 だけで待ち受け、令牌は現在のユーザーだけが読める连接文件に書く。
    bool localBridgeEnabled = true;
    // 設定画面に出す本地桥の状態（地址と连接文件、または起動の失敗）。場景控制器が書く。
    std::string localBridgeStatus{};
};

// Play 中に「Play の結果を残す」で記録した物の姿勢。Play を止めて文書を戻した後、一つの撤销として文書へ書き戻す。
struct KeptPlayPose {
    SceneObjectId id{};
    MATH::Vec3 position{};
    MATH::Vec3 rotationEulerDeg{};
    MATH::Vec3 scale{1.0f, 1.0f, 1.0f};
};

struct EditorContext {
    DebugWindowState windows{};
    EditorAiIntegrationSettings aiIntegration{};
    ComponentGizmoState gizmos{};
    EditorTransformGizmoState transformGizmo{};
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
    std::vector<KeptPlayPose> keptPlayPoses{};
    // 記録した時の場面。Play 中に別の場面へ切り替わっていたら、番号が別の物を指すので書き戻さない。
    std::string keptPlayScenePath{};
    RuntimeResourceRefreshReport lastRuntimeRefreshReport{};
};

} // namespace HIKARI
