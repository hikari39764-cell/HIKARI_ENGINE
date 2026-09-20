#include "Assets/HIKARI_AssetRecord.h"
#include "Editor/DragDrop/HIKARI_EditorAssetDragDrop.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Project/HIKARI_ProjectContext.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif
namespace HIKARI::EDITOR {
bool CinematicsWorkspaceController::DrawResources(DocumentSceneBase &scene,
                                                  CinematicSequence &sequence) {
  bool changed = false;
#if defined(HIKARI_WITH_EDITOR)
  using namespace SEQUENCER;
  if (!ImGui::BeginTabBar("SequenceResources"))
    return false;
  if (ImGui::BeginTabItem("镜头")) {
    if (IconTextButton(EditorGlyph::Add, "创建镜头", "newShot",
                       EditorButtonTone::Primary)) {
      CreateShot(sequence, spaceView_ ? directorViewPanel_.GetViewCamera()
                                      : scene.GetCamera());
      changed = true;
    }
    for (const auto &shot : sequence.cameraCutTrack.clips) {
      ImGui::PushID(static_cast<int>(shot.id));
      char duration[32];
      std::snprintf(duration,sizeof(duration),"  ·  %.1f s",shot.durationSeconds);
      const auto label = shot.camera.name + duration;
      if (ImGui::Selectable(label.c_str(),
                            selectionKind_ == SelectionKind::Shot &&
                                selectedId_ == shot.id)) {
        selectedId_ = shot.id;
        selectionKind_ = SelectionKind::Shot;
        cursor_.Seek(shot.startTimeSeconds);
        previewEnabled_ = true;
      }
      ImGui::PopID();
    }
    if (sequence.cameraCutTrack.clips.empty())
      EmptyState("从一个画面开始", "调整视角，然后创建第一个镜头。");
    ImGui::EndTabItem();
  }
  if (ImGui::BeginTabItem("素材")) {
    static std::array<char, 128> search{};
    SearchField("cueSearch", "搜索特效与声音", search.data(), search.size());
    const auto addCue = [&](const std::string &name, PresentationCueKind kind,
                            const std::string &asset) {
      PresentationCueClip cue;
      for (const auto &c : sequence.presentationCueTrack.clips)
        cue.id = (std::max)(cue.id, c.id + 1);
      cue.name = name;
      cue.kind = kind;
      cue.startSeconds = cursor_.GetTimeSeconds();
      cue.durationSeconds = 3;
      if (kind == PresentationCueKind::Vfx)
        cue.vfxAsset.value = asset;
      else
        cue.soundPath = asset;
      sequence.presentationCueTrack.clips.push_back(cue);
      selectedId_ = cue.id;
      selectionKind_ = SelectionKind::Cue;
      changed = true;
    };
    if (ImGui::CollapsingHeader("特效", ImGuiTreeNodeFlags_DefaultOpen)) {
      for (const auto *record :
           scene.GetAssetDatabase().CollectByType(AssetType::VfxEffect)) {
        const auto name = GetAssetRecordDisplayName(*record);
        if (search[0] && name.find(search.data()) == std::string::npos)
          continue;
        ImGui::PushID(record->guid.value.c_str());
        if (ImGui::Selectable(name.c_str(), false,
                              ImGuiSelectableFlags_AllowDoubleClick) &&
            ImGui::IsMouseDoubleClicked(0))
          addCue(name, PresentationCueKind::Vfx, record->guid.value);
        BeginAssetDragSource(*record);
        ImGui::PopID();
      }
    }
    if (ImGui::CollapsingHeader("声音", ImGuiTreeNodeFlags_DefaultOpen)) {
      const auto root = GetProjectContext().GetRoot();
      const bool refresh = ImGui::SmallButton("刷新声音素材");
      if (audioProjectRoot_ != root || refresh) {
        audioProjectRoot_ = root;
        audioAssets_.clear();
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator
                 it(root / "Assets/Audio", ec),
             end;
             !ec && it != end; it.increment(ec)) {
          if (!it->is_regular_file(ec))
            continue;
          auto extension = it->path().extension().string();
          std::transform(extension.begin(), extension.end(), extension.begin(),
                         [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                         });
          if (extension == ".wav" || extension == ".mp3" || extension == ".ogg")
            audioAssets_.push_back(it->path());
        }
        std::sort(audioAssets_.begin(), audioAssets_.end());
      }
      for (const auto &path : audioAssets_) {
        const auto name = path.stem().string();
        if (search[0] && name.find(search.data()) == std::string::npos)
          continue;
        const auto relative = path.lexically_relative(root).generic_string();
        ImGui::PushID(relative.c_str());
        if (ImGui::Selectable(name.c_str(), false,
                              ImGuiSelectableFlags_AllowDoubleClick) &&
            ImGui::IsMouseDoubleClicked(0))
          addCue(name, PresentationCueKind::Audio, relative);
        if (ImGui::BeginDragDropSource()) {
          ImGui::SetDragDropPayload("HIKARI_SEQUENCE_AUDIO", relative.c_str(),
                                    relative.size() + 1);
          ImGui::TextUnformatted(name.c_str());
          ImGui::EndDragDropSource();
        }
        ImGui::PopID();
      }
    }
    ImGui::TextDisabled("拖到时间轴，或双击加入当前时刻");
    ImGui::EndTabItem();
  }
  if (ImGui::BeginTabItem("模板")) {
    if (ImGui::CollapsingHeader("完整演出", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImGui::Selectable("主体展示 · 远景 / 环绕 / 推进")) {
        const float start = cursor_.GetTimeSeconds();
        for (int i = 0; i < 3; ++i) {
          cursor_.Seek(start + i * 3.0f);
          CreateShot(sequence, scene.GetCamera());
          auto &shot = sequence.cameraCutTrack.clips.back();
          shot.startTimeSeconds = start + i * 3.0f;
          shot.camera.name = i == 0   ? "建立画面"
                             : i == 1 ? "环绕主体"
                                      : "推进收束";
          if (i > 0) {
            CameraMotionEffect e;
            e.kind = i == 1 ? CameraMotionKind::Orbit : CameraMotionKind::Dolly;
            e.amount.keys = {{0, 0, 0, 0}, {3, i == 1 ? 65.0f : 2.0f, 0, 0}};
            shot.camera.motion.push_back(e);
          }
        }
        cursor_.Seek(start);
        changed = true;
      }
    }
    if (ImGui::CollapsingHeader("可复用模块", ImGuiTreeNodeFlags_DefaultOpen)) {
      const char *names[]{"平滑推进", "环绕主体", "轻微手持", "冲击震动"};
      const CameraMotionKind kinds[]{
          CameraMotionKind::Dolly, CameraMotionKind::Orbit,
          CameraMotionKind::Handheld, CameraMotionKind::Shake};
      for (int i = 0; i < 4; ++i)
        if (ImGui::Selectable(names[i])) {
          auto *shot = SelectedShot(sequence);
          if (!shot) {
            CreateShot(sequence, scene.GetCamera());
            shot = SelectedShot(sequence);
          }
          CameraMotionEffect e;
          e.kind = kinds[i];
          for (const auto &existing : shot->camera.motion)
            e.id = (std::max)(e.id, existing.id + 1);
          e.amount.value = i == 2 ? 0.025f : i == 3 ? 0.15f : 1.0f;
          if (i < 2)
            e.amount.keys = {
                {0, 0, 0, 0},
                {shot->durationSeconds, i == 1 ? 90.0f : 2.0f, 0, 0}};
          if (i == 3)
            e.weight.keys = {{0, 1, 0, 0}, {0.4f, 0, 0, 0}};
          shot->camera.motion.push_back(e);
          changed = true;
        }
    }
    ImGui::EndTabItem();
  }
  if (ImGui::BeginTabItem("对象绑定")) {
    if (ImGui::Button("添加对象槽")) {
      const auto name =
          "Subject." +
          std::to_string(AllocateSequenceBindingId(sequence.bindings).value);
      auto id = FindOrCreateSlotBinding(sequence.bindings, name, name);
      FindSequenceBinding(sequence.bindings, id)->required = false;
      changed = true;
    }
    for (auto &binding : sequence.bindings) {
      ImGui::PushID(static_cast<int>(binding.id.value));
      std::array<char, 128> name{};
      std::snprintf(name.data(), name.size(), "%s", binding.name.c_str());
      if (ImGui::InputText("名称", name.data(), name.size())) {
        binding.name = name.data();
        changed = true;
      }
      SceneObjectId current;
      previewBindings_.Resolve(sequence.bindings, binding.id,
                                                 current);
      std::string label = "未绑定";
      for (const auto &object : scene.GetSceneDocument().objects)
        if (object.id == current)
          label = object.name;
      if (ImGui::BeginCombo("预览对象", label.c_str())) {
        for (const auto &object : scene.GetSceneDocument().objects)
          if (ImGui::Selectable(object.name.c_str(), object.id == current)) {
            previewBindings_.Bind(binding.id, object.id);
            previewChanged_ = true;
            if (binding.targetKind == SequenceBindingTargetKind::SceneObject) {
              binding.sceneObjectId = object.id;
              changed = true;
            }
          }
        ImGui::EndCombo();
      }
      changed |= ImGui::Checkbox("运行时必须提供", &binding.required);
      ImGui::Separator();
      ImGui::PopID();
    }
    ImGui::EndTabItem();
  }
  ImGui::EndTabBar();
#endif
  return changed;
}
} // namespace HIKARI::EDITOR
