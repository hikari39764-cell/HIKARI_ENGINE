#pragma once

#include <cstdint>

#include "Editor/Views/HIKARI_EditorViewGuides.h"
#include "Render3D/Core/HIKARI_RenderView.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::EDITOR {

enum class EditorViewRole : uint8_t {
    Game,
    Director,
    Preview,
};

enum class EditorViewCameraSourceKind : uint8_t {
    SceneDirector,
    OwnedEditorCamera,
    SceneCameraObject,
};

enum class EditorViewExecutionMode : uint8_t {
    PrimaryFullQuality,
    SecondaryEditorScene,
    RuntimeParityToolPreview,
};

enum class EditorViewShadingMode : uint8_t {
    Lit,
    Neutral,
    Unlit,
};

struct EditorViewVisualizationState {
    EditorViewShadingMode shadingMode = EditorViewShadingMode::Lit;
    float displayExposure = 1.0f;
    bool showCameraOverlays = true;
    bool showOnlySelectedCamera = false;
    bool showSceneObjectMarkers = true;
    float cameraOverlayScale = 1.0f;
    EditorViewGuides guides{};
};

struct EditorViewportExtent {
    uint32_t width = 0;
    uint32_t height = 0;

    bool IsValid() const noexcept { return width > 0 && height > 0; }

    float Aspect() const noexcept { return height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f; }
};

struct EditorViewportFit {
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
};

inline EditorViewportFit FitEditorViewport(float availableWidth, float availableHeight, float targetAspect) noexcept {
    const float width = availableWidth > 1.0f ? availableWidth : 1.0f;
    const float height = availableHeight > 1.0f ? availableHeight : 1.0f;
    if (targetAspect <= 0.05f) {
        return {0.0f, 0.0f, width, height};
    }

    EditorViewportFit result{};
    const float availableAspect = width / height;
    if (availableAspect > targetAspect) {
        result.height = height;
        result.width = height * targetAspect;
        result.offsetX = (width - result.width) * 0.5f;
    } else {
        result.width = width;
        result.height = width / targetAspect;
        result.offsetY = (height - result.height) * 0.5f;
    }
    return result;
}

struct EditorViewInteractionState {
    bool visible = false;
    bool focused = false;
    bool hovered = false;
    bool mouseCaptured = false;
    bool keyboardActive = false;
    bool gizmoCaptured = false;
};

struct EditorViewCameraBinding {
    EditorViewCameraSourceKind kind = EditorViewCameraSourceKind::SceneDirector;
    SceneObjectId sceneObjectId{};
};

struct EditorViewInstance {
    RENDER3D::RenderViewId renderViewId{};
    EditorViewRole role = EditorViewRole::Game;
    RENDER3D::RenderViewPurpose purpose = RENDER3D::RenderViewPurpose::EditorScene;
    EditorViewCameraBinding cameraBinding{};
    EditorViewExecutionMode execution = EditorViewExecutionMode::PrimaryFullQuality;
    EditorViewVisualizationState visualization{};
    EditorViewportExtent extent{};
    EditorViewInteractionState interaction{};

    MATH::Vec2 overviewCenterXZ{};
    float overviewUnitsPerScreen = 40.0f;
};

inline constexpr RENDER3D::RenderViewId kGameplayCameraObserverRenderViewId{8};
inline constexpr RENDER3D::RenderViewId kGameplayCameraPreviewRenderViewId{9};
// 場面の主视口の右下に出す機位の小窓。
inline constexpr RENDER3D::RenderViewId kSceneCameraInsetRenderViewId{10};
// 镜头演出の時間軸で镜头の上に置いた時刻の画。
inline constexpr RENDER3D::RenderViewId kCinematicsShotPreviewRenderViewId{11};
inline constexpr RENDER3D::RenderViewId kEditorDirectorRenderViewId{2};
inline constexpr RENDER3D::RenderViewId kModelCollisionPreviewRenderViewId{4};
inline constexpr RENDER3D::RenderViewId kModelMaterialPreviewRenderViewId{5};
inline constexpr RENDER3D::RenderViewId kModelMaterialCompareRenderViewId{6};
inline constexpr RENDER3D::RenderViewId kVfxPreviewRenderViewId{7};

} // namespace HIKARI::EDITOR
