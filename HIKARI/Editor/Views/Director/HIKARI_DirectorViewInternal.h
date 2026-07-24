#pragma once

#include "Editor/Views/Director/HIKARI_DirectorViewPanel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

#if defined(HIKARI_WITH_EDITOR)
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#endif
#include "Editor/Views/HIKARI_EditorViewInputGate.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR::DIRECTOR_VIEW {

#if defined(HIKARI_WITH_EDITOR)
constexpr float kMinimumCanvasSize = 64.0f;

const GameObject *FindRuntimeObject(const DocumentSceneBase &scene,
                                    SceneObjectId objectId);

bool HasDocumentParent(const DocumentSceneBase &scene, SceneObjectId objectId);

bool IsEnabledCamera(const DocumentSceneBase &scene, SceneObjectId objectId);

MATH::Vec3 ExtractPosition(const MATH::Mat4 &matrix);

SceneObjectId DrawCameraOverlays(
    const DocumentSceneBase &scene, SceneObjectId selectedObjectId,
    const Camera3D &viewCamera, const ImVec2 &origin, const ImVec2 &size,
    bool acceptSelection, const EditorViewVisualizationState &visualization);

const char *ModeLabel(DirectorViewMode mode);

const char *ShadingModeLabel(EditorViewShadingMode mode);

RENDER3D::EDITORVIEW::EditorInteractiveShadingMode
ResolveShadingMode(EditorViewShadingMode mode);

const char *
CameraOverlayModeLabel(const EditorViewVisualizationState &visualization);

bool DrawModeButton(const char *label, bool selected);

uint32_t ExtentDimension(float value);
#endif

} // namespace HIKARI::EDITOR::DIRECTOR_VIEW
