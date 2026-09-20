#include "Core/HIKARI_TimeService.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Play/HIKARI_EditorPlaySession.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

namespace HIKARI::EDITOR {
void CinematicsWorkspaceController::DrawDockSpace(bool reset) const {
#if defined(HIKARI_WITH_EDITOR)
  if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable))
    return;
  const auto *viewport = ImGui::GetMainViewport();
  // v5 は旧 Camera/Overview 配置で使用済み。新しい pane ID と同時に移行する。
  const auto id = ImGui::GetID("HIKARI_CinematicsDockSpace_v6");
  if (reset || !ImGui::DockBuilderGetNode(id)) {
    ImGui::DockBuilderRemoveNode(id);
    ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodePos(id, viewport->WorkPos);
    ImGui::DockBuilderSetNodeSize(id, viewport->WorkSize);
    ImGuiID center = id, timeline = 0, left = 0, right = 0;
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.31f, &timeline,
                                &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.19f, &left, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.29f, &right, &center);
    ImGui::DockBuilderDockWindow("素材与镜头###Cinematics/Resources", left);
    ImGui::DockBuilderDockWindow("画面###Cinematics/Monitor", center);
    ImGui::DockBuilderDockWindow("属性与效果###Cinematics/Inspector", right);
    ImGui::DockBuilderDockWindow("演出时间轴###Cinematics/Timeline", timeline);
    ImGui::DockBuilderFinish(id);
  }
  ImGui::DockSpaceOverViewport(id, viewport);
#endif
}

CinematicsWorkspaceResult CinematicsWorkspaceController::Draw(
    DocumentSceneBase &scene, EditorPlaySession &, EditorContext &context,
    SelectionSyncService &, EditorWorkspaceHost &workspaceHost) {
  CinematicsWorkspaceResult result;
  result.statusMessage.swap(pendingStatusMessage_);
#if defined(HIKARI_WITH_EDITOR)
  if (sequenceDocumentController_.ConsumeTimelineResetRequested())
    ResetEditing(scene);
  auto &document = sequenceDocumentController_.GetDocument();
  bool enabled = !scene.IsRuntimePlayActive();
  if (ImGui::Begin("素材与镜头###Cinematics/Resources", nullptr,
                   ImGuiWindowFlags_NoCollapse)) {
    auto toolbar = sequenceDocumentToolbar_.Draw(
        sequenceDocumentController_, scene.GetAssetDatabase(),
        &ActiveSequence(scene), scene.GetCurrentSceneDisplayName(),
        sequenceLibraryVisible_, enabled);
    result.saveSceneRequested = toolbar.saveEmbeddedSceneRequested;
    if (toolbar.toggleLibraryRequested)
      sequenceLibraryVisible_ = !sequenceLibraryVisible_;
    if (!toolbar.statusMessage.empty())
      result.statusMessage = toolbar.statusMessage;
    if (sequenceDocumentController_.ConsumeTimelineResetRequested())
      ResetEditing(scene);
    auto &settings = document.GetSettings(scene.GetSceneDocument().cinematics);
    if (settings.sequences.size() > 1 &&
        ImGui::BeginCombo("当前演出", ActiveSequence(scene).name.c_str())) {
      for (const auto &item : settings.sequences) {
        ImGui::PushID(static_cast<int>(item.id.value));
        if (ImGui::Selectable(item.name.c_str(),
                              item.id == activeSequenceId_)) {
          ResetEditing(scene);
          activeSequenceId_ = item.id;
        }
        ImGui::PopID();
      }
      ImGui::EndCombo();
    }
    if (sequenceLibraryVisible_ && ImGui::CollapsingHeader("演出资产")) {
      auto library = sequenceLibraryPanel_.Draw(
          scene.GetAssetDatabase(), document.GetAssetGuid(), enabled);
      if (library.action == SequenceLibraryActionKind::OpenAsset)
        RequestOpenSequenceAsset(scene, library.assetGuid,
                                 result.statusMessage);
      if (library.action == SequenceLibraryActionKind::NewAsset)
        RequestNewSequenceAsset(scene, result.statusMessage);
    }
  }
  ImGui::End();
  auto before = document.GetSettings(scene.GetSceneDocument().cinematics);
  auto &sequence = ActiveSequence(scene);
  if (!cursor_.IsBound())
    cursor_.Bind(sequence.durationSeconds);
  cursor_.SetDuration(sequence.durationSeconds);
  bool changed = false;
  if (ImGui::Begin("素材与镜头###Cinematics/Resources")) {
    ImGui::BeginDisabled(!enabled);
    changed |= DrawResources(scene, sequence);
    ImGui::EndDisabled();
  }
  ImGui::End();
  if (ImGui::Begin("演出时间轴###Cinematics/Timeline", nullptr,
                   ImGuiWindowFlags_NoCollapse)) {
    ImGui::BeginDisabled(!enabled);
    changed |= DrawTransport(sequence);
    changed |= DrawTimeline(scene, sequence);
    ImGui::EndDisabled();
  }
  ImGui::End();
  if (ImGui::Begin("属性与效果###Cinematics/Inspector", nullptr,
                   ImGuiWindowFlags_NoCollapse)) {
    ImGui::BeginDisabled(!enabled);
    changed |= DrawInspector(scene, sequence);
    ImGui::EndDisabled();
  }
  ImGui::End();
  DrawMonitor(scene, workspaceHost);
  if (spaceView_) {
    auto &view = workspaceHost.GetCinematicsDirectorView();
    if (ImGui::Begin("空间编辑###Cinematics/Space", &spaceView_)) {
      ImGui::BeginDisabled(!enabled);
      if (IconTextButton(EditorGlyph::Camera, "从此视角创建镜头",
                         "shotFromSpace")) {
        CreateShot(sequence, directorViewPanel_.GetViewCamera());
        changed = true;
      }
      ImGui::SameLine();
      if (IconTextButton(EditorGlyph::Keyframe,
                         autoKey_ ? "记录当前关键帧" : "更新首帧构图",
                         "captureFromSpace")) {
        CapturePose(scene, sequence, directorViewPanel_.GetViewCamera());
        changed = true;
      }
      ImGui::EndDisabled();
      // 空间观察不直接改写场景对象；记录动作通过当前演出文档提交。
      directorViewPanel_.SetTargetCamera({});
      directorViewPanel_.DrawContents(scene, view, {}, context.transformGizmo,
                                      TIME::GetFrameContext().unscaledDt, !enabled);
    }
    ImGui::End();
  } else {
    RENDER3D::EDITORVIEW::ClearRequest(
        workspaceHost.GetCinematicsDirectorView().renderViewId);
  }
  if (changed) {
    NormalizeCinematicSequence(sequence);
    previewChanged_ = true;
    if (document.IsEmbeddedScene())
      result.cinematicsChanged = true;
    else
      document.RecordApplied(std::move(before), mergeId_);
  }
  if (ImGui::IsAnyItemActive())
    result.timelineEditMergeId = mergeId_;
  else {
    document.SealMerge();
    ++mergeId_;
  }
  if (enabled && previewEnabled_) {
    auto evaluation = cursor_.Tick(ImGui::GetIO().DeltaTime,
                                   SEQUENCER::SequenceEvaluationMode::Preview);
    if (!scene.PreviewSequence(sequence, previewBindings_,
                               evaluation, previewChanged_)) {
      pendingStatusMessage_ = "演出预览未就绪：请检查对象绑定与素材。";
      previewEnabled_ = false;
      if (ownsPreview_) {
        scene.EndSequencePreview();
        ownsPreview_ = false;
      }
    }
    ownsPreview_ = previewEnabled_;
    previewChanged_ = false;
  } else if (ownsPreview_) {
    scene.EndSequencePreview();
    ownsPreview_ = false;
  }
  if (!result.statusMessage.empty()) {
    if (ImGui::Begin("演出时间轴###Cinematics/Timeline"))
      ImGui::TextWrapped("%s", result.statusMessage.c_str());
    ImGui::End();
  }
  sequenceDocumentToolbar_.DrawPendingConfirmation(sequenceDocumentController_,
                                                   scene.GetAssetDatabase(),
                                                   result.statusMessage);
#endif
  return result;
}
} // namespace HIKARI::EDITOR
