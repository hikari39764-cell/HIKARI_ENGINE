#include "Core/Text/HIKARI_Localization.h"
#include "Editor/HIKARI_EditorViewportInput.h"
#include "Editor/Views/HIKARI_EditorViewInputGate.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Views/HIKARI_EditorViewportGeometry.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Runtime/Presentation/HIKARI_FramePresentationService.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Vfx/Post/HIKARI_PostSystem.h"
#include <algorithm>
#include <cmath>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif
namespace HIKARI::EDITOR {
void CinematicsWorkspaceController::DrawMonitor(DocumentSceneBase& scene, EditorWorkspaceHost& host,
                                                CinematicsWorkspaceResult& result) {
#if defined(HIKARI_WITH_EDITOR)
    auto& view = host.GetCinematicsGameView();
    view.cameraBinding.kind = EditorViewCameraSourceKind::SceneDirector;
    if (!ImGui::Begin(TEXT::LocalizeWindow("cine.view", "Cinematics/Monitor").c_str(), nullptr,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse)) {
        view.extent = {};
        SERVICES::SetEditorGameViewportSize(0, 0, false);
        ClearGameViewportInputRect();
        ImGui::End();
        return;
    }
    if (IconToggleButton(EditorGlyph::WorldSpace, "spaceView", spaceView_, {26, 26},
                         TEXT::Localize("cine.stage").c_str())) {
        spaceView_ = !spaceView_;
        if (spaceView_)
            directorViewPanel_.FrameTargetCamera(scene, {});
    }
    NextToolbarItem(130);
    if (IconToggleButton(EditorGlyph::Keyframe, "autoKey", autoKey_, {26, 26}, TEXT::Localize("cine.autoKey").c_str()))
        autoKey_ = !autoKey_;
    NextToolbarItem(130);
    StatusBadge(scene.IsRuntimePlayActive() ? TEXT::Localize("cine.gameRunning").c_str()
                : previewEnabled_           ? TEXT::Localize("cine.previewing").c_str()
                                            : TEXT::Localize("cine.freeView").c_str(),
                previewEnabled_ ? EditorStatusTone::Ready : EditorStatusTone::Normal);
    NextToolbarItem(130);
    ImGui::TextDisabled("%s", autoKey_ ? TEXT::Localize("cine.recordKeys").c_str()
                                       : TEXT::Localize("cine.updateBase").c_str());
    if (scene.IsRuntimePlayActive())
        ImGui::TextWrapped("%s", TEXT::Localize("cine.playInputHelp").c_str());
    auto size = ImGui::GetContentRegionAvail();
    size.x = (std::max)(size.x, 1.0f);
    size.y = (std::max)(size.y, 1.0f);
    const auto fit = FitEditorViewport(size.x, size.y, 16.0f / 9.0f);
    auto origin = ImGui::GetCursorScreenPos();
    origin.x += fit.offsetX;
    origin.y += fit.offsetY;
    ImGui::SetCursorScreenPos(origin);
    view.extent = {static_cast<uint32_t>(fit.width), static_cast<uint32_t>(fit.height)};
    view.interaction.visible = true;
    SERVICES::SetEditorGameViewportSize(static_cast<int>(fit.width), static_cast<int>(fit.height), true);
    const auto srv = POST::PostSystem::GetEditorViewportSrv();
    const bool ready = POST::PostSystem::IsEditorViewportReady() && srv.ptr;
    if (ready)
        ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(srv.ptr)), {fit.width, fit.height});
    else {
        ImGui::Dummy({fit.width, fit.height});
        ImGui::GetWindowDrawList()->AddText({origin.x + 20, origin.y + 20}, IM_COL32(170, 180, 190, 255),
                                            TEXT::Localize("cine.waitingFrame").c_str());
    }
    if (scene.IsRuntimePlayActive() && ready) {
        SetGameViewportInputRect(origin.x, origin.y, fit.width, fit.height, ImGui::IsWindowFocused());
        SetGameViewportGizmoCapture(false);
        result.playViewport = {static_cast<int32_t>(std::lround(origin.x)), static_cast<int32_t>(std::lround(origin.y)),
                               static_cast<int32_t>(std::lround(origin.x + fit.width)),
                               static_cast<int32_t>(std::lround(origin.y + fit.height))};
        result.playViewportClicked = ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                                     !QueryEditorViewInputBlockState().pointer;
    } else
        ClearGameViewportInputRect();
    ImGui::End();
#endif
}
} // namespace HIKARI::EDITOR
