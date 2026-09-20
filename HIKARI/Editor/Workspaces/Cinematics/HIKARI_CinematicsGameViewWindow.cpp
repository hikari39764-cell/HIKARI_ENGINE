#include "Editor/HIKARI_EditorViewportInput.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Views/HIKARI_EditorViewportGeometry.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Runtime/Presentation/HIKARI_FramePresentationService.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Vfx/Post/HIKARI_PostSystem.h"
#include <algorithm>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif
namespace HIKARI::EDITOR {
void CinematicsWorkspaceController::DrawMonitor(DocumentSceneBase &scene,
                                                EditorWorkspaceHost &host) {
#if defined(HIKARI_WITH_EDITOR)
  auto &view = host.GetCinematicsGameView();
  view.cameraBinding.kind = EditorViewCameraSourceKind::SceneDirector;
  if (!ImGui::Begin("画面###Cinematics/Monitor", nullptr,
                    ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoCollapse)) {
    view.extent = {};
    SERVICES::SetEditorGameViewportSize(0, 0, false);
    ClearGameViewportInputRect();
    ImGui::End();
    return;
  }
  if (IconToggleButton(EditorGlyph::WorldSpace, "spaceView", spaceView_,
                       {26, 26}, "空间编辑"))
    {
      spaceView_ = !spaceView_;
      if (spaceView_) directorViewPanel_.FrameTargetCamera(scene,{});
    }
  ImGui::SameLine();
  if (IconToggleButton(EditorGlyph::Keyframe, "autoKey", autoKey_, {26, 26},
                       "自动关键帧：记录调整到当前时间点"))
    autoKey_ = !autoKey_;
  ImGui::SameLine();
  StatusBadge(scene.IsRuntimePlayActive() ? "游戏运行"
              : previewEnabled_           ? "演出预览"
                                          : "自由观察",
              previewEnabled_ ? EditorStatusTone::Ready
                              : EditorStatusTone::Normal);
  ImGui::SameLine();
  ImGui::TextDisabled("%s", autoKey_ ? "记录关键帧" : "更新首帧构图");
  auto size = ImGui::GetContentRegionAvail();
  size.x = (std::max)(size.x, 1.0f);
  size.y = (std::max)(size.y, 1.0f);
  const auto fit = FitEditorViewport(size.x, size.y, 16.0f / 9.0f);
  auto origin = ImGui::GetCursorScreenPos();
  origin.x += fit.offsetX;
  origin.y += fit.offsetY;
  ImGui::SetCursorScreenPos(origin);
  view.extent = {static_cast<uint32_t>(fit.width),
                 static_cast<uint32_t>(fit.height)};
  view.interaction.visible = true;
  SERVICES::SetEditorGameViewportSize(static_cast<int>(fit.width),
                                      static_cast<int>(fit.height), true);
  const auto srv = POST::PostSystem::GetEditorViewportSrv();
  if (POST::PostSystem::IsEditorViewportReady() && srv.ptr)
    ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(srv.ptr)),
                 {fit.width, fit.height});
  else {
    ImGui::Dummy({fit.width, fit.height});
    ImGui::GetWindowDrawList()->AddText({origin.x + 20, origin.y + 20},
                                        IM_COL32(170, 180, 190, 255),
                                        "等待场景画面");
  }
  if (scene.IsRuntimePlayActive())
    SetGameViewportInputRect(origin.x, origin.y, fit.width, fit.height,
                             ImGui::IsWindowFocused());
  else
    ClearGameViewportInputRect();
  ImGui::End();
#endif
}
} // namespace HIKARI::EDITOR
