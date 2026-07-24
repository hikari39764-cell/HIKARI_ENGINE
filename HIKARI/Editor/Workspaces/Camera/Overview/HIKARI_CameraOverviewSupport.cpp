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
#include "Editor/Workspaces/Camera/Overview/HIKARI_CameraOverviewInternal.h"

namespace HIKARI::EDITOR::CAMERA_OVERVIEW {

#if defined(HIKARI_WITH_EDITOR)
MATH::Vec3 ExtractAxis(const MATH::Mat4 &matrix, int column) {
  return {matrix.m[column][0], matrix.m[column][1], matrix.m[column][2]};
}

const SceneObjectData *FindDocumentObject(const SceneDocument &document,
                                          SceneObjectId objectId) {

  const auto it = std::find_if(document.objects.begin(), document.objects.end(),
                               [objectId](const SceneObjectData &object) {
                                 return object.id == objectId;
                               });
  return it == document.objects.end() ? nullptr : &*it;
}

GameObject *FindRuntimeObject(DocumentSceneBase &scene,
                              SceneObjectId objectId) {
  return scene.GetWorld().FindObject(objectId);
}

void SelectRuntimeObject(EditorContext &context, GameObject *object) {
  if (object == nullptr) {
    context.selection.ClearObjects();
  } else if (World *world = object->GetWorld()) {
    context.selection.SelectObject(*world, object);
  }
}

std::vector<OverviewCameraEntry> GatherCameras(DocumentSceneBase &scene,
                                               const EditorContext &context) {

  std::vector<OverviewCameraEntry> cameras{};
  const SceneDocument &document = scene.GetSceneDocument();
  const SceneObjectId selectedId =
      context.selection.selectedObject
          ? context.selection.selectedObject->GetDocumentId()
          : SceneObjectId{};
  const SceneObjectId previewId = scene.IsEditorCameraPreviewActive()
                                      ? scene.GetEditorCameraPreviewObjectId()
                                      : SceneObjectId{};
  const CameraDirectorStatus directorStatus =
      scene.GetCameraDirector().GetStatus();

  for (const auto &object : scene.GetWorld().GetObjects()) {
    if (!object) {
      continue;
    }
    const CameraComponent *camera = object->GetComponent<CameraComponent>();
    if (!camera) {
      continue;
    }

    const MATH::Mat4 worldMatrix = object->GetTransform().GetWorldMatrix();
    const MATH::Vec3 position = ExtractAxis(worldMatrix, 3);
    const MATH::Vec3 forward = ExtractAxis(worldMatrix, 2);
    MATH::Vec2 forwardXZ{forward.x, forward.z};
    const float forwardLength =
        std::sqrt(forwardXZ.x * forwardXZ.x + forwardXZ.y * forwardXZ.y);
    if (forwardLength > kVectorEpsilon) {
      forwardXZ.x /= forwardLength;
      forwardXZ.y /= forwardLength;
    } else {
      forwardXZ = {0.0f, 1.0f};
    }

    OverviewCameraEntry entry{};
    entry.objectId = object->GetDocumentId();
    entry.name = object->GetName();
    entry.positionXZ = {position.x, position.z};
    entry.forwardXZ = forwardXZ;
    entry.fovYRad = camera->GetFovYRad();
    entry.nearClip = camera->GetNearClip();
    entry.farClip = camera->GetFarClip();
    entry.enabled = camera->IsEnabled();
    entry.isActive =
        directorStatus.activeSourceCameraObjectId == entry.objectId;
    entry.isDefault = document.camera.defaultCameraObjectId == entry.objectId;
    entry.isSelected = selectedId == entry.objectId;
    entry.isPreviewed = previewId == entry.objectId;
    if (const SceneObjectData *documentObject =
            FindDocumentObject(document, entry.objectId)) {
      entry.hasParent = documentObject->parent.has_value();
    }
    cameras.push_back(std::move(entry));
  }

  std::sort(cameras.begin(), cameras.end(),
            [](const OverviewCameraEntry &lhs, const OverviewCameraEntry &rhs) {
              if (lhs.name != rhs.name) {
                return lhs.name < rhs.name;
              }
              return lhs.objectId.value < rhs.objectId.value;
            });
  return cameras;
}

uint32_t ToExtentDimension(float value) {
  if (!std::isfinite(value) || value <= 0.0f) {
    return 0;
  }
  const float limit =
      static_cast<float>((std::numeric_limits<uint32_t>::max)());
  return static_cast<uint32_t>((std::min)(value, limit));
}

float NiceGridStep(float targetStep) {
  targetStep = (std::max)(targetStep, 0.0001f);
  const float magnitude = std::pow(10.0f, std::floor(std::log10(targetStep)));
  const float normalized = targetStep / magnitude;
  if (normalized <= 1.0f) {
    return magnitude;
  }
  if (normalized <= 2.0f) {
    return 2.0f * magnitude;
  }
  if (normalized <= 5.0f) {
    return 5.0f * magnitude;
  }
  return 10.0f * magnitude;
}

ImVec2 WorldToScreen(const MATH::Vec2 &world, const MATH::Vec2 &center,
                     const ImVec2 &canvasCenter, float pixelsPerUnit) {

  return {canvasCenter.x + (world.x - center.x) * pixelsPerUnit,
          canvasCenter.y - (world.y - center.y) * pixelsPerUnit};
}

MATH::Vec2 ScreenToWorld(const ImVec2 &screen, const MATH::Vec2 &center,
                         const ImVec2 &canvasCenter, float pixelsPerUnit) {

  return {center.x + (screen.x - canvasCenter.x) / pixelsPerUnit,
          center.y - (screen.y - canvasCenter.y) / pixelsPerUnit};
}

void FitCameras(EditorViewInstance &view,
                const std::vector<OverviewCameraEntry> &cameras) {

  if (cameras.empty()) {
    view.overviewCenterXZ = {};
    view.overviewUnitsPerScreen = 40.0f;
    return;
  }

  MATH::Vec2 minimum = cameras.front().positionXZ;
  MATH::Vec2 maximum = cameras.front().positionXZ;
  for (const OverviewCameraEntry &camera : cameras) {
    minimum.x = (std::min)(minimum.x, camera.positionXZ.x);
    minimum.y = (std::min)(minimum.y, camera.positionXZ.y);
    maximum.x = (std::max)(maximum.x, camera.positionXZ.x);
    maximum.y = (std::max)(maximum.y, camera.positionXZ.y);
  }

  view.overviewCenterXZ = {(minimum.x + maximum.x) * 0.5f,
                           (minimum.y + maximum.y) * 0.5f};
  const float spanX = maximum.x - minimum.x;
  const float spanZ = maximum.y - minimum.y;
  const float aspect = view.extent.IsValid()
                           ? (std::max)(view.extent.Aspect(), 0.1f)
                           : 16.0f / 9.0f;
  const float fittedWidth = (std::max)(spanX, spanZ * aspect);
  view.overviewUnitsPerScreen =
      std::clamp((std::max)(fittedWidth * 1.35f, 12.0f), kMinOverviewUnits,
                 kMaxOverviewUnits);
}

const OverviewCameraEntry *
FindSelectedCamera(const std::vector<OverviewCameraEntry> &cameras) {

  const auto found = std::find_if(
      cameras.begin(), cameras.end(),
      [](const OverviewCameraEntry &camera) { return camera.isSelected; });
  return found == cameras.end() ? nullptr : &*found;
}

ImU32 ResolveCameraColor(const OverviewCameraEntry &camera) {
  if (!camera.enabled) {
    return IM_COL32(154, 162, 174, 235);
  }
  if (camera.isSelected) {
    return IM_COL32(255, 206, 108, 255);
  }
  if (camera.isDefault) {
    return IM_COL32(96, 235, 158, 255);
  }
  return IM_COL32(105, 205, 255, 255);
}

void DrawGrid(ImDrawList &drawList, const ImVec2 &canvasMin,
              const ImVec2 &canvasMax, const ImVec2 &canvasCenter,
              const EditorViewInstance &view, float pixelsPerUnit) {

  const float gridStep = NiceGridStep(view.overviewUnitsPerScreen / 12.0f);
  const float halfWidthWorld = view.overviewUnitsPerScreen * 0.5f;
  const float halfHeightWorld =
      halfWidthWorld / (std::max)(view.extent.Aspect(), 0.1f);
  const float minX = view.overviewCenterXZ.x - halfWidthWorld;
  const float maxX = view.overviewCenterXZ.x + halfWidthWorld;
  const float minZ = view.overviewCenterXZ.y - halfHeightWorld;
  const float maxZ = view.overviewCenterXZ.y + halfHeightWorld;

  const ImU32 minorColor = IM_COL32(96, 112, 130, 145);
  const ImU32 axisColor = IM_COL32(158, 178, 198, 225);
  int lineCount = 0;
  for (float x = std::floor(minX / gridStep) * gridStep;
       x <= maxX && lineCount < 256; x += gridStep, ++lineCount) {
    const ImVec2 screen =
        WorldToScreen({x, view.overviewCenterXZ.y}, view.overviewCenterXZ,
                      canvasCenter, pixelsPerUnit);
    const ImU32 color =
        std::abs(x) <= gridStep * 0.01f ? axisColor : minorColor;
    drawList.AddLine({screen.x, canvasMin.y}, {screen.x, canvasMax.y}, color,
                     std::abs(x) <= gridStep * 0.01f ? 1.5f : 1.0f);
  }

  lineCount = 0;
  for (float z = std::floor(minZ / gridStep) * gridStep;
       z <= maxZ && lineCount < 256; z += gridStep, ++lineCount) {
    const ImVec2 screen =
        WorldToScreen({view.overviewCenterXZ.x, z}, view.overviewCenterXZ,
                      canvasCenter, pixelsPerUnit);
    const ImU32 color =
        std::abs(z) <= gridStep * 0.01f ? axisColor : minorColor;
    drawList.AddLine({canvasMin.x, screen.y}, {canvasMax.x, screen.y}, color,
                     std::abs(z) <= gridStep * 0.01f ? 1.5f : 1.0f);
  }
}

void DrawSceneObjectPoints(ImDrawList &drawList, const DocumentSceneBase &scene,
                           const EditorViewInstance &view,
                           const ImVec2 &canvasCenter, float pixelsPerUnit) {

  for (const auto &object : scene.GetWorld().GetObjects()) {
    if (!object || object->GetComponent<CameraComponent>() != nullptr) {
      continue;
    }
    const MATH::Vec3 position =
        ExtractAxis(object->GetTransform().GetWorldMatrix(), 3);
    const ImVec2 screen =
        WorldToScreen({position.x, position.z}, view.overviewCenterXZ,
                      canvasCenter, pixelsPerUnit);
    drawList.AddCircleFilled(screen, 2.6f, IM_COL32(186, 198, 211, 195), 8);
  }
}

void DrawCameraMarker(ImDrawList &drawList, const OverviewCameraEntry &camera,
                      const EditorViewInstance &view,
                      const ImVec2 &canvasCenter, float pixelsPerUnit) {

  const ImVec2 position = WorldToScreen(
      camera.positionXZ, view.overviewCenterXZ, canvasCenter, pixelsPerUnit);
  const ImU32 color = ResolveCameraColor(camera);
  const float overlayScale =
      std::clamp(view.visualization.cameraOverlayScale, 0.5f, 2.0f);
  const float frustumWorldLength =
      std::clamp(camera.farClip * 0.08f, view.overviewUnitsPerScreen * 0.025f,
                 view.overviewUnitsPerScreen * 0.14f) *
      overlayScale;
  const float halfAngle = std::clamp(camera.fovYRad * 0.5f, 0.05f, 1.45f);
  const float cosine = std::cos(halfAngle);
  const float sine = std::sin(halfAngle);
  const MATH::Vec2 leftDirection{
      camera.forwardXZ.x * cosine - camera.forwardXZ.y * sine,
      camera.forwardXZ.x * sine + camera.forwardXZ.y * cosine};
  const MATH::Vec2 rightDirection{
      camera.forwardXZ.x * cosine + camera.forwardXZ.y * sine,
      -camera.forwardXZ.x * sine + camera.forwardXZ.y * cosine};
  const ImVec2 forwardEnd =
      WorldToScreen(camera.positionXZ + camera.forwardXZ * frustumWorldLength,
                    view.overviewCenterXZ, canvasCenter, pixelsPerUnit);
  const ImVec2 leftEnd =
      WorldToScreen(camera.positionXZ + leftDirection * frustumWorldLength,
                    view.overviewCenterXZ, canvasCenter, pixelsPerUnit);
  const ImVec2 rightEnd =
      WorldToScreen(camera.positionXZ + rightDirection * frustumWorldLength,
                    view.overviewCenterXZ, canvasCenter, pixelsPerUnit);

  drawList.AddLine(position, leftEnd, color, 1.35f);
  drawList.AddLine(position, rightEnd, color, 1.35f);
  drawList.AddLine(leftEnd, rightEnd, color, 1.1f);
  drawList.AddLine(position, forwardEnd, color, 2.0f);
  drawList.AddCircleFilled(position, 5.0f * overlayScale, color, 12);
  if (camera.isDefault) {
    drawList.AddCircle(position, 8.0f * overlayScale,
                       IM_COL32(105, 240, 164, 255), 16, 1.5f);
  }
  if (camera.isPreviewed) {
    drawList.AddCircle(position, 10.5f * overlayScale,
                       IM_COL32(208, 157, 255, 255), 16, 1.4f);
  }

  std::string label = camera.name.empty()
                          ? ("Camera " + std::to_string(camera.objectId.value))
                          : camera.name;
  if (!camera.enabled) {
    label += " [Disabled]";
  }
  drawList.AddText(
      {position.x + 9.0f * overlayScale, position.y - 8.0f * overlayScale},
      color, label.c_str());
}

CameraOverviewPanelResult MakeAction(CameraOverviewActionKind kind,
                                     SceneObjectId cameraObjectId) {

  CameraOverviewPanelResult result{};
  result.action.kind = kind;
  result.action.cameraObjectId = cameraObjectId;
  return result;
}
#endif

} // namespace HIKARI::EDITOR::CAMERA_OVERVIEW
