#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif
namespace HIKARI::EDITOR {
bool CinematicsWorkspaceController::DrawInspector(DocumentSceneBase &scene,
                                                  CinematicSequence &sequence) {
  bool changed = false;
#if defined(HIKARI_WITH_EDITOR)
  using namespace SEQUENCER;
  const auto text = [](const char *label, std::string &value) {
    std::array<char, 1024> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
    if (ImGui::InputText(label, buffer.data(), buffer.size())) {
      value = buffer.data();
      return true;
    }
    return false;
  };
  const auto binding = [&](const char *label, SequenceBindingId &id) {
    const auto *current = FindSequenceBinding(sequence.bindings, id);
    bool result = false;
    if (ImGui::BeginCombo(label,
                          current ? current->name.c_str() : "无 / 世界空间")) {
      if (ImGui::Selectable("无 / 世界空间", !id.IsValid())) {
        id = {};
        result = true;
      }
      for (const auto &b : sequence.bindings)
        if (ImGui::Selectable(b.name.c_str(), b.id == id)) {
          id = b.id;
          result = true;
        }
      ImGui::EndCombo();
    }
    return result;
  };
  if (auto *shot = SelectedShot(sequence)) {
    PanelTitle("镜头", shot->camera.name.c_str());
    changed |= text("名称", shot->camera.name);
    changed |=
        ImGui::DragFloat("开始时间", &shot->startTimeSeconds, 0.02f, 0, 10000);
    changed |= ImGui::DragFloat("镜头时长", &shot->durationSeconds, 0.02f, 0.1f,
                                10000);
    if (IconTextButton(EditorGlyph::Duplicate, "复制", "duplicateShot")) {
      auto copy = *shot;
      copy.id = AllocateCameraCutClipId(sequence.cameraCutTrack);
      copy.startTimeSeconds += copy.durationSeconds;
      copy.camera.name += " Copy";
      selectedId_ = copy.id;
      sequence.cameraCutTrack.clips.push_back(std::move(copy));
      return true;
    }
    ImGui::SameLine();
    if (IconButton(EditorGlyph::Delete, "deleteShot", EditorButtonTone::Danger,
                   {26, 26}, "删除镜头")) {
      std::erase_if(sequence.cameraCutTrack.clips,
                    [&](const auto &s) { return s.id == selectedId_; });
      selectedId_ = 0;
      return true;
    }
    if (!shot->camera.owned) {
      ImGui::TextWrapped(
          "此镜头引用场景摄像机；转换后可独立编辑，不会改变场景摄像机。");
      if (ImGui::Button("转换为演出内镜头（60 fps 采样）")) {
        SceneObjectId id;
        Camera3D camera;
        if (previewBindings_.Resolve(
                sequence.bindings, shot->cameraBindingId, id) &&
            scene.TryResolveCameraObjectView(id, 16.0f / 9, camera)) {
          std::vector<ShotPose> poses;
          shot->camera.nearClip = camera.GetNearZ();
          shot->camera.farClip = camera.GetFarZ();
          const int count =
              static_cast<int>(std::ceil(shot->durationSeconds * 60));
          for (int frame = 0; frame <= count; ++frame) {
            const float local =
                (std::min)(frame / 60.0f, shot->durationSeconds);
            CinematicCameraEvaluation evaluation;
            evaluation.shotId=shot->id;
            evaluation.cameraObjectId=id;
            evaluation.transform = EvaluateCameraTransformTrack(
                sequence.cameraTransformTrack, shot->cameraBindingId,
                shot->startTimeSeconds + local);
            evaluation.lens = EvaluateCameraLensTrack(
                sequence.cameraLensTrack, shot->cameraBindingId,
                shot->startTimeSeconds + local);
            auto sample = camera;
            if (evaluation.HasCameraAnimation())
              BuildEvaluatedCinematicCamera(evaluation, camera, 16.0f / 9,
                                            sample);
            auto pose = CaptureShotPose(sample, local);
            pose.incoming = pose.outgoing = 1;
            poses.push_back(pose);
            shot->camera.nearClip =
                (std::min)(shot->camera.nearClip, sample.GetNearZ());
            shot->camera.farClip =
                (std::max)(shot->camera.farClip, sample.GetFarZ());
          }
          shot->camera.poses = std::move(poses);
          shot->camera.owned = true;
          changed = true;
        }
      }
    }
    if (shot->camera.owned &&
        ImGui::CollapsingHeader("构图与关键帧",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
      changed |= binding("跟随坐标系", shot->camera.anchor);
      changed |= binding("注视对象", shot->camera.lookAt);
      changed |=
          ImGui::DragFloat3("注视偏移", &shot->camera.targetOffset.x, 0.02f);
      const float local =
          std::clamp(cursor_.GetTimeSeconds() - shot->startTimeSeconds, 0.0f,
                     shot->durationSeconds);
      if (ImGui::Button("在当前时间添加姿态关键帧")) {
        auto base = shot->camera;
        base.motion.clear();
        auto pose =
            CaptureShotPose(EvaluateShotCamera(base, local, 16.0f / 9), local);
        auto it = std::find_if(
            shot->camera.poses.begin(), shot->camera.poses.end(),
            [&](const auto &p) { return std::abs(p.time - local) < 0.0001f; });
        if (it == shot->camera.poses.end())
          shot->camera.poses.push_back(pose);
        else
          *it = pose;
        changed = true;
      }
      int remove = -1;
      for (size_t i = 0; i < shot->camera.poses.size(); ++i) {
        auto &pose = shot->camera.poses[i];
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::TreeNodeEx("pose", ImGuiTreeNodeFlags_DefaultOpen,
                              "姿态 %.2f s", pose.time)) {
          changed |= ImGui::DragFloat("时间", &pose.time, 0.01f, 0,
                                      shot->durationSeconds);
          changed |= ImGui::DragFloat3("位置", &pose.eye.x, 0.025f);
          changed |= ImGui::DragFloat3("看向", &pose.target.x, 0.025f);
          changed |= ImGui::SliderFloat("视野", &pose.fov, 1, 150, "%.1f°");
          changed |= ImGui::DragFloat("倾斜", &pose.roll, 0.1f);
          changed |= ImGui::DragFloat("进入速度", &pose.incoming, 0.05f);
          changed |= ImGui::DragFloat("离开速度", &pose.outgoing, 0.05f);
          if (ImGui::SmallButton("等速")) {
            pose.incoming = pose.outgoing = 1;
            changed = true;
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("缓入缓出")) {
            pose.incoming = pose.outgoing = 0;
            changed = true;
          }
          if (shot->camera.poses.size() > 1) {
            ImGui::SameLine();
            if (ImGui::SmallButton("删除"))
              remove = static_cast<int>(i);
          }
          ImGui::TreePop();
        }
        ImGui::PopID();
      }
      if (remove >= 0) {
        shot->camera.poses.erase(shot->camera.poses.begin() + remove);
        changed = true;
      }
    }
    if (shot->camera.owned &&
        ImGui::CollapsingHeader("运动效果链", ImGuiTreeNodeFlags_DefaultOpen)) {
      constexpr const char *names[]{"推拉", "侧移", "升降", "环绕", "摇镜",
                                    "俯仰", "倾斜", "震动", "手持"};
      if (ImGui::Button("添加运动"))
        ImGui::OpenPopup("motionMenu");
      if (ImGui::BeginPopup("motionMenu")) {
        for (int i = 0; i < 9; ++i)
          if (ImGui::Selectable(names[i])) {
            CameraMotionEffect e;
            e.kind = static_cast<CameraMotionKind>(i);
            for (const auto &old : shot->camera.motion)
              e.id = (std::max)(e.id, old.id + 1);
            e.amount.value = i >= 7 ? 0.05f : 1.0f;
            shot->camera.motion.push_back(e);
            changed = true;
          }
        ImGui::EndPopup();
      }
      int remove = -1, move = -1;
      for (size_t i = 0; i < shot->camera.motion.size(); ++i) {
        auto &e = shot->camera.motion[i];
        ImGui::PushID(static_cast<int>(e.id));
        changed |= ImGui::Checkbox("##enabled", &e.enabled);
        ImGui::SameLine();
        const bool open =
            ImGui::TreeNodeEx("motion", ImGuiTreeNodeFlags_DefaultOpen, "%s",
                              names[static_cast<int>(e.kind)]);
        ImGui::SameLine();
        if (IconButton(EditorGlyph::Delete, "remove", EditorButtonTone::Quiet,
                       {20, 20}, "删除模块"))
          remove = static_cast<int>(i);
        if (i) {
          ImGui::SameLine();
          if (ImGui::SmallButton("上移"))
            move = static_cast<int>(i);
        }
        if (open) {
          const float local = (std::max)(0.0f, cursor_.GetTimeSeconds() -
                                                   shot->startTimeSeconds);
          ImGui::PushID("amount");
          changed |= DrawCurve(e.amount, local);
          ImGui::PopID();
          ImGui::PushID("weight");
          ImGui::TextDisabled("权重");
          changed |= DrawCurve(e.weight, local);
          ImGui::PopID();
          if (e.kind == CameraMotionKind::Shake ||
              e.kind == CameraMotionKind::Handheld)
            changed |= ImGui::DragFloat("频率", &e.frequency, 0.05f, 0, 100);
          ImGui::TreePop();
        }
        ImGui::PopID();
      }
      if (remove >= 0) {
        shot->camera.motion.erase(shot->camera.motion.begin() + remove);
        changed = true;
      } else if (move > 0) {
        std::swap(shot->camera.motion[move], shot->camera.motion[move - 1]);
        changed = true;
      }
    }
    if (ImGui::CollapsingHeader("镜头画面效果"))
      changed |= DrawEffects(
          shot->effects,
          (std::max)(0.0f, cursor_.GetTimeSeconds() - shot->startTimeSeconds));
    if (ImGui::CollapsingHeader("镜头衔接")) {
      bool blend = shot->transition.mode == CameraCutTransitionMode::EaseInOut;
      if (ImGui::Checkbox("平滑运镜过渡", &blend)) {
        shot->transition.mode = blend ? CameraCutTransitionMode::EaseInOut
                                      : CameraCutTransitionMode::Cut;
        changed = true;
      }
      if (blend)
        changed |=
            ImGui::DragFloat("过渡时间", &shot->transition.durationSeconds,
                             0.02f, 0, shot->durationSeconds);
      ImGui::TextDisabled("画面淡入淡出使用画面效果片段。");
    }
  } else if (selectionKind_ == SelectionKind::Cue) {
    for (auto &cue : sequence.presentationCueTrack.clips)
      if (cue.id == selectedId_) {
        PanelTitle(cue.kind == PresentationCueKind::Vfx ? "特效片段"
                                                        : "声音片段");
        changed |= text("名称", cue.name);
        changed |= ImGui::DragFloat("开始", &cue.startSeconds, 0.02f, 0, 10000);
        changed |=
            ImGui::DragFloat("时长", &cue.durationSeconds, 0.02f, 0.01f, 10000);
        changed |= binding("绑定对象", cue.targetBinding);
        if (cue.kind == PresentationCueKind::Vfx) {
          AssetFieldOptions options;
          options.label = "特效";
          options.requiredType = AssetType::VfxEffect;
          changed |= DrawAssetField(&scene.GetAssetDatabase(), options,
                                    cue.vfxAsset.value);
        } else {
          changed |= text("声音文件", cue.soundPath);
          changed |= ImGui::SliderFloat("音量", &cue.audio.volume, 0, 1);
        }
        if (ImGui::Button("删除片段")) {
          std::erase_if(sequence.presentationCueTrack.clips,
                        [&](const auto &c) { return c.id == selectedId_; });
          selectedId_ = 0;
          return true;
        }
        break;
      }
  } else if (selectionKind_ == SelectionKind::Effect) {
    PanelTitle("演出画面效果", "作用于演出输出，不影响编辑器界面");
    changed |= DrawEffects(sequence.screenEffects, cursor_.GetTimeSeconds());
  } else {
    PanelTitle("演出");
    changed |= text("名称", sequence.name);
    changed |=
        ImGui::DragFloat("时长", &sequence.durationSeconds, 0.1f, 1, 10000);
    ImGui::TextWrapped("选择时间轴中的片段来编辑。素材可以拖到时间轴；运动和画"
                       "面效果可以保存为演出资产复用。");
  }
#endif
  return changed;
}
} // namespace HIKARI::EDITOR
