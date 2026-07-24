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

namespace HIKARI::EDITOR {

using namespace CAMERA_OVERVIEW;

CameraOverviewPanelResult
CameraOverviewPanel::DrawCameraListContents(DocumentSceneBase &scene,
                                            EditorContext &context) const {

#if defined(HIKARI_WITH_EDITOR)
  CameraOverviewPanelResult result{};
  std::vector<OverviewCameraEntry> cameras = GatherCameras(scene, context);

  StatusBadge((std::to_string(cameras.size()) + " cameras").c_str(),
              EditorStatusTone::Normal);
  const CameraDirectorStatus directorStatus =
      scene.GetCameraDirector().GetStatus();
  if (directorStatus.overrideCount > 0u || directorStatus.blending) {
    ImGui::SameLine();
    ImGui::TextDisabled("| %zu override%s%s", directorStatus.overrideCount,
                        directorStatus.overrideCount == 1u ? "" : "s",
                        directorStatus.blending ? " | blending" : "");
  }
  if (scene.IsEditorCameraPreviewActive()) {
    ImGui::SameLine();
    if (IconTextButton(EditorGlyph::Exit, "Exit View", "CameraListExitView",
                       EditorButtonTone::Quiet, ImVec2(0.0f, 26.0f),
                       "Exit camera preview")) {
      return MakeAction(CameraOverviewActionKind::ExitView,
                        scene.GetEditorCameraPreviewObjectId());
    }
  }

  if (cameras.empty()) {
    ImGui::Separator();
    ImGui::TextDisabled("No CameraComponent exists in the current scene.");
    return result;
  }

  const ImGuiTableFlags tableFlags =
      ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
      ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
  if (!ImGui::BeginTable("CinematicsCameraList", 4, tableFlags)) {
    return result;
  }

  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn("Camera", ImGuiTableColumnFlags_WidthStretch, 1.2f);
  ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 110.0f);
  ImGui::TableSetupColumn("Lens", ImGuiTableColumnFlags_WidthFixed, 190.0f);
  ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 112.0f);
  ImGui::TableHeadersRow();

  for (const OverviewCameraEntry &camera : cameras) {
    const std::string cameraIdScope =
        "Camera:" + std::to_string(camera.objectId.value);
    ImGui::PushID(cameraIdScope.c_str());
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    const std::string label =
        camera.name.empty()
            ? ("Camera " + std::to_string(camera.objectId.value))
            : camera.name;
    if (ImGui::Selectable(label.c_str(), camera.isSelected,
                          ImGuiSelectableFlags_SpanAllColumns |
                              ImGuiSelectableFlags_AllowOverlap)) {
      SelectRuntimeObject(context, FindRuntimeObject(scene, camera.objectId));
      result = MakeAction(CameraOverviewActionKind::Select, camera.objectId);
    }

    ImGui::TableSetColumnIndex(1);
    if (!camera.enabled) {
      ImGui::TextDisabled("Disabled");
    } else if (camera.isPreviewed) {
      ImGui::TextColored(ImVec4(0.78f, 0.56f, 1.0f, 1.0f), "Preview");
    } else if (camera.isActive) {
      ImGui::TextColored(ImVec4(0.28f, 0.82f, 0.92f, 1.0f), "Active");
    } else if (camera.isDefault) {
      ImGui::TextColored(ImVec4(0.35f, 0.87f, 0.58f, 1.0f), "Default");
    } else {
      ImGui::TextUnformatted("Ready");
    }

    ImGui::TableSetColumnIndex(2);
    ImGui::Text("%.1f deg | %.3f / %.1f", camera.fovYRad * kRadiansToDegrees,
                camera.nearClip, camera.farClip);

    ImGui::TableSetColumnIndex(3);
    if (camera.isDefault) {
      ImGui::BeginDisabled();
    }
    if (IconButton(EditorGlyph::Camera, "CameraSetDefault",
                   EditorButtonTone::Quiet, ImVec2(26.0f, 26.0f),
                   camera.isDefault ? "Default camera"
                                    : "Set as default camera")) {
      result =
          MakeAction(CameraOverviewActionKind::SetDefault, camera.objectId);
    }
    if (camera.isDefault) {
      ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (!camera.enabled) {
      ImGui::BeginDisabled();
    }
    if (IconButton(camera.isPreviewed ? EditorGlyph::Exit : EditorGlyph::Reveal,
                   "CameraPreview",
                   camera.isPreviewed ? EditorButtonTone::Primary
                                      : EditorButtonTone::Quiet,
                   ImVec2(26.0f, 26.0f),
                   camera.isPreviewed ? "Exit camera preview"
                                      : "Look through camera")) {
      result =
          MakeAction(camera.isPreviewed ? CameraOverviewActionKind::ExitView
                                        : CameraOverviewActionKind::ViewThrough,
                     camera.objectId);
    }
    if (!camera.enabled) {
      ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (camera.hasParent) {
      ImGui::BeginDisabled();
    }
    if (IconButton(EditorGlyph::Snap, "CameraSnapToView",
                   EditorButtonTone::Quiet, ImVec2(26.0f, 26.0f),
                   camera.hasParent ? "Parented cameras cannot be snapped"
                                    : "Snap camera to editor view")) {
      result =
          MakeAction(CameraOverviewActionKind::SnapToView, camera.objectId);
    }
    if (camera.hasParent) {
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Snap is available for root Camera objects only.");
      }
    }

    ImGui::PopID();
  }

  ImGui::EndTable();
  return result;
#else
  (void)scene;
  (void)context;
  return {};
#endif
}

} // namespace HIKARI::EDITOR
