#pragma once

#include "Editor/Workspaces/Camera/Overview/HIKARI_CameraOverviewPanel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Views/HIKARI_EditorViewInputGate.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR::CAMERA_OVERVIEW {

#if defined(HIKARI_WITH_EDITOR)

//---------------------------------------------------------
// Overviewパネルの定数定義
//---------------------------------------------------------
constexpr float kMinOverviewUnits = 2.0f;
constexpr float kMaxOverviewUnits = 10000.0f;
constexpr float kMinCanvasSize = 1.0f;
constexpr float kCameraHitRadius = 12.0f;
constexpr float kVectorEpsilon = 0.00001f;
constexpr float kRadiansToDegrees = 57.29577951308232f;

// OverviewCameraEntry構造体の定義
struct OverviewCameraEntry {
  SceneObjectId objectId{};
  std::string name{};
  MATH::Vec2 positionXZ{};
  MATH::Vec2 forwardXZ{0.0f, 1.0f};
  float fovYRad = 1.0471975512f;
  float nearClip = 0.1f;
  float farClip = 100.0f;
  bool enabled = true;
  bool isActive = false;
  bool isDefault = false;
  bool isSelected = false;
  bool isPreviewed = false;
  bool hasParent = false;
};

MATH::Vec3 ExtractAxis(const MATH::Mat4 &matrix, int column);

const SceneObjectData *FindDocumentObject(const SceneDocument &document,
                                          SceneObjectId objectId);

GameObject *FindRuntimeObject(DocumentSceneBase &scene, SceneObjectId objectId);

void SelectRuntimeObject(EditorContext &context, GameObject *object);

std::vector<OverviewCameraEntry> GatherCameras(DocumentSceneBase &scene,
                                               const EditorContext &context);

uint32_t ToExtentDimension(float value);

float NiceGridStep(float targetStep);

ImVec2 WorldToScreen(const MATH::Vec2 &world, const MATH::Vec2 &center,
                     const ImVec2 &canvasCenter, float pixelsPerUnit);

MATH::Vec2 ScreenToWorld(const ImVec2 &screen, const MATH::Vec2 &center,
                         const ImVec2 &canvasCenter, float pixelsPerUnit);

void FitCameras(EditorViewInstance &view,
                const std::vector<OverviewCameraEntry> &cameras);

const OverviewCameraEntry *
FindSelectedCamera(const std::vector<OverviewCameraEntry> &cameras);

ImU32 ResolveCameraColor(const OverviewCameraEntry &camera);

void DrawGrid(ImDrawList &drawList, const ImVec2 &canvasMin,
              const ImVec2 &canvasMax, const ImVec2 &canvasCenter,
              const EditorViewInstance &view, float pixelsPerUnit);

void DrawSceneObjectPoints(ImDrawList &drawList, const DocumentSceneBase &scene,
                           const EditorViewInstance &view,
                           const ImVec2 &canvasCenter, float pixelsPerUnit);

void DrawCameraMarker(ImDrawList &drawList, const OverviewCameraEntry &camera,
                      const EditorViewInstance &view,
                      const ImVec2 &canvasCenter, float pixelsPerUnit);

CameraOverviewPanelResult MakeAction(CameraOverviewActionKind kind,
                                     SceneObjectId cameraObjectId);
#endif

} // namespace HIKARI::EDITOR::CAMERA_OVERVIEW
