#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Editor/Authoring/HIKARI_SequenceCameraEditing.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include <algorithm>
#include <cmath>
#include "imgui.h"

namespace HIKARI::EDITOR {
bool CinematicsWorkspaceController::DrawBoundCameraInspector(DocumentSceneBase& scene, CinematicSequence& sequence,
                                                             SEQUENCER::CameraCutClip& shot) {
    const float time = shot.startTimeSeconds + ShotLocalTime(shot);
    BoundCameraPose pose;
    if (!ReadBoundCameraPose(sequence, shot.cameraBindingId, time, pose)) {
        ImGui::TextWrapped("该镜头缺少启用的位置或视角轨道，请检查资产。");
        return false;
    }
    // 外部轨道を読み込むだけでは自有镜头へ変換しない。UI の選択は文書履歴にも入れない。
    const auto seek = [&](float t) {
        cursor_.Pause();
        cursor_.Seek(t);
        previewEnabled_ = true;
    };
    if (ImGui::Button("起点机位"))
        seek(shot.startTimeSeconds);
    ImGui::SameLine();
    if (ImGui::Button("终点机位"))
        seek(shot.startTimeSeconds + shot.durationSeconds);
    const auto times = BoundCameraKeyTimes(sequence, shot.cameraBindingId);
    if (ImGui::Button("上一关键帧")) {
        for (auto it = times.rbegin(); it != times.rend(); ++it)
            if (*it < time - 1.e-5f) {
                seek(*it);
                break;
            }
    }
    ImGui::SameLine();
    if (ImGui::Button("下一关键帧"))
        for (float t : times)
            if (t > time + 1.e-5f) {
                seek(t);
                break;
            }
    ImGui::Text("当前 %.3f 秒 · 保留原始轨道与插值", time);
    uint32_t parts = 0;
    if (ImGui::DragFloat3("位置", &pose.position.x, .01f))
        parts |= BoundPosition;
    if (ImGui::DragFloat3("旋转 XYZ（度）", &pose.rotationEulerDeg.x, .1f))
        parts |= BoundRotation;
    if (ImGui::DragFloat("垂直视角", &pose.fov, .1f, 1.f, 179.f))
        parts |= BoundFov;
    if (ImGui::Button("在当前时间添加 / 更新关键帧"))
        parts = BoundAll;
    bool changed = parts && WriteBoundCameraPose(sequence, shot.cameraBindingId, time, pose, parts);
    if (ImGui::Button("用空间视图设定当前机位")) {
        CapturePose(scene, sequence, directorViewPanel_.GetViewCamera());
        changed = true;
    }
    const auto& document = GetSequenceDocument();
    if (const auto* group = document.FindCameraEndpointGroup(shot.cameraBindingId, time)) {
        ImGui::Separator();
        ImGui::TextWrapped("当前区域：%s · 关联 %zu 处机位", group->label.c_str(), group->endpoints.size());
        const bool matches = document.CameraEndpointsMatch(*group);
        ImGui::TextWrapped(matches ? "修改此端点后，保存会同步到其他关联演出。"
                                   : "关联机位存在差异；可用当前画面统一，或调整此端点后保存。");
        ImGui::BeginDisabled(changed || group->endpoints.size() < 2);
        if (ImGui::Button("以当前机位统一此区域"))
            AlignCameraEndpoint(shot.cameraBindingId, group->endpoints.front().end, pendingStatusMessage_);
        ImGui::EndDisabled();
    }
    const bool endpoint =
        time <= shot.startTimeSeconds + 1.e-5f || time >= shot.startTimeSeconds + shot.durationSeconds - 1.e-5f;
    ImGui::BeginDisabled(endpoint);
    if (ImGui::Button("删除当前关键帧"))
        changed |= RemoveBoundCameraKey(sequence, shot.cameraBindingId, time);
    ImGui::EndDisabled();
    if (changed) {
        cursor_.Pause();
        previewEnabled_ = true;
        previewChanged_ = true;
    }
    return changed;
}
} // namespace HIKARI::EDITOR
