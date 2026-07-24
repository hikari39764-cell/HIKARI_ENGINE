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
#include "Editor/Views/Director/HIKARI_DirectorViewInternal.h"
#include "Editor/Views/HIKARI_EditorViewInputGate.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR::DIRECTOR_VIEW {

#if defined(HIKARI_WITH_EDITOR)
constexpr float kCameraHitDistance = 7.0f;
constexpr float kRadiansToDegrees = 57.2957795131f;

struct CameraOverlayEntry {
  SceneObjectId objectId{};
  std::string name{};
  std::array<MATH::Vec3, 9> points{};
  bool enabled = false;
  bool selected = false;
};

struct CameraOverlayBasis {
  MATH::Vec3 position{};
  MATH::Vec3 right{1.0f, 0.0f, 0.0f};
  MATH::Vec3 up{0.0f, 1.0f, 0.0f};
  MATH::Vec3 forward{0.0f, 0.0f, 1.0f};
};

const GameObject *FindRuntimeObject(const DocumentSceneBase &scene,
                                    SceneObjectId objectId) {

  return scene.GetWorld().FindObject(objectId);
}

bool HasDocumentParent(const DocumentSceneBase &scene, SceneObjectId objectId) {

  for (const SceneObjectData &object : scene.GetSceneDocument().objects) {
    if (object.id == objectId) {
      return object.parent.has_value();
    }
  }
  return false;
}

bool IsEnabledCamera(const DocumentSceneBase &scene, SceneObjectId objectId) {

  const GameObject *object = FindRuntimeObject(scene, objectId);
  const CameraComponent *camera =
      object ? object->GetComponent<CameraComponent>() : nullptr;
  return camera != nullptr && camera->IsEnabled();
}

MATH::Vec3 ExtractPosition(const MATH::Mat4 &matrix) {
  return {matrix.m[3][0], matrix.m[3][1], matrix.m[3][2]};
}

CameraOverlayBasis BuildCameraOverlayBasis(const MATH::Mat4 &world) {
  constexpr float epsilon = 1.0e-5f;

  CameraOverlayBasis basis{};
  basis.position = ExtractPosition(world);
  basis.forward =
      MATH::Normalize({world.m[2][0], world.m[2][1], world.m[2][2]});
  if (MATH::Length(basis.forward) <= epsilon) {
    basis.forward = {0.0f, 0.0f, 1.0f};
  }

  MATH::Vec3 upCandidate =
      MATH::Normalize({world.m[1][0], world.m[1][1], world.m[1][2]});
  if (MATH::Length(upCandidate) <= epsilon ||
      std::abs(MATH::Dot(upCandidate, basis.forward)) >= 0.999f) {
    upCandidate = {0.0f, 1.0f, 0.0f};
  }
  if (std::abs(MATH::Dot(upCandidate, basis.forward)) >= 0.999f) {
    upCandidate = {1.0f, 0.0f, 0.0f};
  }

  basis.right = MATH::Normalize(MATH::Cross(upCandidate, basis.forward));
  basis.up = MATH::Normalize(MATH::Cross(basis.forward, basis.right));
  return basis;
}

MATH::Vec3 TransformCameraOverlayPoint(const CameraOverlayBasis &basis, float x,
                                       float y, float z) {

  return basis.position + basis.right * x + basis.up * y + basis.forward * z;
}

bool ProjectWorldToViewport(const Camera3D &camera,
                            const MATH::Vec3 &worldPosition,
                            const ImVec2 &viewportOrigin,
                            const ImVec2 &viewportSize,
                            ImVec2 &outScreenPosition) {

  const MATH::Vec4 clip = camera.GetViewProj().TransformPoint(
      {worldPosition.x, worldPosition.y, worldPosition.z, 1.0f});
  if (std::abs(clip.w) <= 1.0e-5f) {
    return false;
  }

  const float inverseW = 1.0f / clip.w;
  const float ndcX = clip.x * inverseW;
  const float ndcY = clip.y * inverseW;
  const float ndcZ = clip.z * inverseW;
  if (ndcZ < 0.0f || ndcZ > 1.0f) {
    return false;
  }

  outScreenPosition = {viewportOrigin.x + (ndcX * 0.5f + 0.5f) * viewportSize.x,
                       viewportOrigin.y +
                           (-ndcY * 0.5f + 0.5f) * viewportSize.y};
  return true;
}

float DistanceSquaredToSegment(const ImVec2 &point, const ImVec2 &start,
                               const ImVec2 &end) {

  const float segmentX = end.x - start.x;
  const float segmentY = end.y - start.y;
  const float lengthSquared = segmentX * segmentX + segmentY * segmentY;
  if (lengthSquared <= 1.0e-5f) {
    const float dx = point.x - start.x;
    const float dy = point.y - start.y;
    return dx * dx + dy * dy;
  }
  const float amount = std::clamp(
      ((point.x - start.x) * segmentX + (point.y - start.y) * segmentY) /
          lengthSquared,
      0.0f, 1.0f);
  const float nearestX = start.x + segmentX * amount;
  const float nearestY = start.y + segmentY * amount;
  const float dx = point.x - nearestX;
  const float dy = point.y - nearestY;
  return dx * dx + dy * dy;
}

std::vector<CameraOverlayEntry>
GatherCameraOverlays(const DocumentSceneBase &scene,
                     SceneObjectId selectedObjectId, float aspect,
                     float overlayScale) {

  std::vector<CameraOverlayEntry> result{};
  const float safeOverlayScale = std::clamp(overlayScale, 0.5f, 2.0f);
  for (const auto &object : scene.GetWorld().GetObjects()) {
    if (!object) {
      continue;
    }
    const CameraComponent *camera = object->GetComponent<CameraComponent>();
    if (!camera) {
      continue;
    }

    const float nearDistance = camera->GetNearClip() * safeOverlayScale;
    const float farDistance =
        (std::min)(camera->GetFarClip(),
                   (std::max)(3.0f, camera->GetNearClip() * 3.0f)) *
        safeOverlayScale;
    const float tangent = std::tan(camera->GetFovYRad() * 0.5f);
    const float nearHalfHeight = tangent * nearDistance;
    const float nearHalfWidth = nearHalfHeight * aspect;
    const float farHalfHeight = tangent * farDistance;
    const float farHalfWidth = farHalfHeight * aspect;
    const MATH::Mat4 world = object->GetTransform().GetWorldMatrix();
    const CameraOverlayBasis basis = BuildCameraOverlayBasis(world);

    CameraOverlayEntry entry{};
    entry.objectId = object->GetDocumentId();
    entry.name = object->GetName();
    entry.enabled = camera->IsEnabled();
    entry.selected = entry.objectId == selectedObjectId;
    entry.points = {
        basis.position,
        TransformCameraOverlayPoint(basis, -nearHalfWidth, -nearHalfHeight,
                                    nearDistance),
        TransformCameraOverlayPoint(basis, nearHalfWidth, -nearHalfHeight,
                                    nearDistance),
        TransformCameraOverlayPoint(basis, nearHalfWidth, nearHalfHeight,
                                    nearDistance),
        TransformCameraOverlayPoint(basis, -nearHalfWidth, nearHalfHeight,
                                    nearDistance),
        TransformCameraOverlayPoint(basis, -farHalfWidth, -farHalfHeight,
                                    farDistance),
        TransformCameraOverlayPoint(basis, farHalfWidth, -farHalfHeight,
                                    farDistance),
        TransformCameraOverlayPoint(basis, farHalfWidth, farHalfHeight,
                                    farDistance),
        TransformCameraOverlayPoint(basis, -farHalfWidth, farHalfHeight,
                                    farDistance),
    };
    result.push_back(std::move(entry));
  }
  return result;
}

SceneObjectId DrawCameraOverlays(
    const DocumentSceneBase &scene, SceneObjectId selectedObjectId,
    const Camera3D &viewCamera, const ImVec2 &origin, const ImVec2 &size,
    bool acceptSelection, const EditorViewVisualizationState &visualization) {

  if (!visualization.showCameraOverlays) {
    return {};
  }

  constexpr std::array<std::array<int, 2>, 16> kEdges{{
      {1, 2},
      {2, 3},
      {3, 4},
      {4, 1},
      {5, 6},
      {6, 7},
      {7, 8},
      {8, 5},
      {1, 5},
      {2, 6},
      {3, 7},
      {4, 8},
      {0, 5},
      {0, 6},
      {0, 7},
      {0, 8},
  }};

  ImDrawList *drawList = ImGui::GetWindowDrawList();
  drawList->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
  const ImVec2 mouse = ImGui::GetMousePos();
  SceneObjectId nearestObject{};
  float nearestDistanceSquared = kCameraHitDistance * kCameraHitDistance;

  const std::vector<CameraOverlayEntry> cameras =
      GatherCameraOverlays(scene, selectedObjectId, viewCamera.GetAspect(),
                           visualization.cameraOverlayScale);
  for (const CameraOverlayEntry &camera : cameras) {
    if (visualization.showOnlySelectedCamera && !camera.selected) {
      continue;
    }
    const ImU32 color = !camera.enabled
                            ? IM_COL32(150, 158, 170, 220)
                            : (camera.selected ? IM_COL32(255, 204, 104, 255)
                                               : IM_COL32(108, 224, 255, 245));
    std::array<ImVec2, 9> screenPoints{};
    std::array<bool, 9> projected{};
    for (size_t index = 0; index < camera.points.size(); ++index) {
      projected[index] = ProjectWorldToViewport(
          viewCamera, camera.points[index], origin, size, screenPoints[index]);
    }

    for (const auto &edge : kEdges) {
      if (!projected[edge[0]] || !projected[edge[1]]) {
        continue;
      }
      drawList->AddLine(screenPoints[edge[0]], screenPoints[edge[1]], color,
                        camera.selected ? 1.8f : 1.1f);
      if (acceptSelection) {
        const float distanceSquared = DistanceSquaredToSegment(
            mouse, screenPoints[edge[0]], screenPoints[edge[1]]);
        if (distanceSquared < nearestDistanceSquared) {
          nearestDistanceSquared = distanceSquared;
          nearestObject = camera.objectId;
        }
      }
    }

    if (projected[0]) {
      drawList->AddCircleFilled(screenPoints[0], 4.5f, color, 10);
      const std::string label =
          camera.name.empty()
              ? "Camera " + std::to_string(camera.objectId.value)
              : camera.name;
      const ImVec2 textPosition{screenPoints[0].x + 8.0f,
                                screenPoints[0].y - 8.0f};
      drawList->AddText(textPosition, color, label.c_str());
      if (acceptSelection) {
        const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
        const bool labelHovered =
            mouse.x >= textPosition.x - 3.0f &&
            mouse.y >= textPosition.y - 3.0f &&
            mouse.x <= textPosition.x + textSize.x + 3.0f &&
            mouse.y <= textPosition.y + textSize.y + 3.0f;
        const float dx = mouse.x - screenPoints[0].x;
        const float dy = mouse.y - screenPoints[0].y;
        const float pointDistanceSquared = dx * dx + dy * dy;
        if (labelHovered || pointDistanceSquared < nearestDistanceSquared) {
          nearestDistanceSquared = labelHovered ? 0.0f : pointDistanceSquared;
          nearestObject = camera.objectId;
        }
      }
    }
  }
  drawList->PopClipRect();
  return nearestObject;
}

const char *ModeLabel(DirectorViewMode mode) {
  switch (mode) {
  case DirectorViewMode::LookThrough:
    return "Look Through";
  case DirectorViewMode::Pilot:
    return "Pilot";
  case DirectorViewMode::Free:
  default:
    return "Free";
  }
}

const char *ShadingModeLabel(EditorViewShadingMode mode) {
  switch (mode) {
  case EditorViewShadingMode::Neutral:
    return "Neutral";
  case EditorViewShadingMode::Unlit:
    return "Unlit";
  case EditorViewShadingMode::Lit:
  default:
    return "Lit";
  }
}

RENDER3D::EDITORVIEW::EditorInteractiveShadingMode
ResolveShadingMode(EditorViewShadingMode mode) {

  switch (mode) {
  case EditorViewShadingMode::Neutral:
    return RENDER3D::EDITORVIEW::EditorInteractiveShadingMode::Neutral;
  case EditorViewShadingMode::Unlit:
    return RENDER3D::EDITORVIEW::EditorInteractiveShadingMode::Unlit;
  case EditorViewShadingMode::Lit:
  default:
    return RENDER3D::EDITORVIEW::EditorInteractiveShadingMode::Lit;
  }
}

const char *
CameraOverlayModeLabel(const EditorViewVisualizationState &visualization) {

  if (!visualization.showCameraOverlays) {
    return "Off";
  }
  return visualization.showOnlySelectedCamera ? "Selected" : "All";
}

bool DrawModeButton(const char *label, bool selected) {
  if (selected) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.38f, 0.58f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(0.22f, 0.46f, 0.68f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(0.16f, 0.34f, 0.52f, 1.0f));
  }
  const bool clicked = ImGui::Button(label);
  if (selected) {
    ImGui::PopStyleColor(3);
  }
  return clicked;
}

uint32_t ExtentDimension(float value) {
  if (!std::isfinite(value) || value <= 0.0f) {
    return 0;
  }
  return static_cast<uint32_t>((std::min)(
      value, static_cast<float>((std::numeric_limits<uint32_t>::max)())));
}
#endif

} // namespace HIKARI::EDITOR::DIRECTOR_VIEW
