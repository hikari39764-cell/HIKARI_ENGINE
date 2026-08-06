#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include "Editor/AI/HIKARI_AI_AssistantPanel.h"

#include <utility>

#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

namespace HIKARI::EDITOR {

void ModelMaterialWorkspaceController::DrawDockSpace(
    bool resetDefaultDockLayout) const {
#if defined(HIKARI_WITH_EDITOR)
  ImGuiIO &io = ImGui::GetIO();
  if ((io.ConfigFlags & ImGuiConfigFlags_DockingEnable) == 0) {
    return;
  }
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const ImGuiID dockspaceId = ImGui::GetID("HIKARI_ModelMaterialDockSpace_v6");
  if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr ||
      resetDefaultDockLayout) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

    ImGuiID center = dockspaceId;
    ImGuiID left = 0u;
    ImGuiID right = 0u;
    ImGuiID preview = 0u;
    ImGuiID properties = 0u;
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, &left, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.34f, &right,
                                &center);
    ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.46f, &properties,
                                &preview);

    ImGui::DockBuilderDockWindow("Preview###ModelMaterial/Preview", preview);
    ImGui::DockBuilderDockWindow("Material Graph###ModelMaterial/Graph",
                                 center);
    ImGui::DockBuilderDockWindow(
        "Materials & Parts###ModelMaterial/Slots", left);
    ImGui::DockBuilderDockWindow(
        "Properties###ModelMaterial/Material", properties);
    ImGui::DockBuilderDockWindow("Instance###ModelMaterial/Instance", properties);
    ImGui::DockBuilderDockWindow("Ramp###ModelMaterial/Ramp", properties);
    ImGui::DockBuilderDockWindow("Diagnostics###ModelMaterial/Diagnostics",
                                  properties);
    ImGui::DockBuilderDockWindow("Stats###ModelMaterial/Stats", properties);
    ImGui::DockBuilderFinish(dockspaceId);
  }
  ImGui::DockSpaceOverViewport(dockspaceId, viewport);
#else
  (void)resetDefaultDockLayout;
#endif
}

ModelMaterialWorkspaceResult ModelMaterialWorkspaceController::Draw(
    DocumentSceneBase &scene, EditorWorkspaceHost &workspaceHost,
    EditorCommandRouter &commandRouter) {
  ModelMaterialWorkspaceResult result{};
  result.statusMessage = statusMessage_;
#if defined(HIKARI_WITH_EDITOR)
  RefreshAssetChoices(scene.GetAssetDatabase());
  if (materialGesture_ && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    CancelMaterialGesture(scene);
  } else if (authoringDocument_.IsAbPreviewActive() &&
             !ImGui::GetIO().WantTextInput &&
             ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    if (authoringDocument_.DiscardAbPreview(statusMessage_)) {
      (void)RefreshMaterialPreviews(scene, statusMessage_);
    }
  }
  DrawPreviewWindow(scene, workspaceHost, result, commandRouter);
  DrawModelAndSlotsWindow(scene);
  DrawMaterialWindow(scene);
  DrawGraphWindow(scene);
  if (authoringDocument_.HasMaterial()) {
    const MaterialAssetKind kind =
        authoringDocument_.GetMaterial().kind;
    if (kind == MaterialAssetKind::Instance) {
      DrawInstanceWindow(scene);
    } else if (kind == MaterialAssetKind::Ramp) {
      DrawRampWindow(scene);
    }
    if (!authoringDocument_.GetCompileResult().success) {
      DrawDiagnosticsWindow(scene);
    }
  }
  DrawPendingTransitionModal(scene);
  FlushPendingMaterialPreview(scene);
  if (aiAssistantPanel_ != nullptr &&
      aiAssistantOpenFlag_ != nullptr &&
      *aiAssistantOpenFlag_) {
    aiAssistantPanel_->Draw(*aiAssistantOpenFlag_);
  }
#else
  (void)scene;
  (void)workspaceHost;
  (void)commandRouter;
#endif
  result.statusMessage = statusMessage_;
  return result;
}

bool ModelMaterialWorkspaceController::QueuePendingTransition(
    PendingTransition transition, std::string &outMessage) {
  if (transition.kind == PendingTransitionKind::None) {
    return false;
  }
  if (pendingTransition_.kind != PendingTransitionKind::None) {
    outMessage = "Finish the pending Save / Discard / Cancel decision first";
    return false;
  }
  pendingTransition_ = std::move(transition);
  pendingTransitionPopupRequested_ = true;
  outMessage = "Unsaved authoring changes need a Save / Discard / Cancel decision";
  return true;
}

bool ModelMaterialWorkspaceController::SaveForPendingTransition(
    DocumentSceneBase &scene) {
  if (authoringDocument_.IsAbPreviewActive()) {
    if (!authoringDocument_.KeepAbPreview(statusMessage_)) {
      return false;
    }
    if (!RefreshMaterialPreviews(scene, statusMessage_)) {
      return false;
    }
  }
  if (authoringDocument_.IsMaterialDirty() &&
      !SaveMaterial(scene, false, statusMessage_)) {
    return false;
  }
  if (authoringDocument_.IsBindingDirty() &&
      !SaveBindings(scene, false, statusMessage_)) {
    return false;
  }
  return true;
}

void ModelMaterialWorkspaceController::DiscardForPendingTransition(
    DocumentSceneBase &scene) {
  if (materialGesture_) {
    CancelMaterialGesture(scene);
  }
  const bool discardedMaterial =
      authoringDocument_.DiscardMaterialChanges();
  const bool discardedBindings =
      authoringDocument_.DiscardBindingChanges();
  if (discardedBindings) {
    RebuildSlotState(scene.GetAssetDatabase(), scene.GetAssetRegistry());
  }
  if (discardedMaterial) {
    (void)RefreshMaterialPreviews(scene, statusMessage_);
  }
}

bool ModelMaterialWorkspaceController::ExecutePendingTransition(
    DocumentSceneBase &scene) {
  if (pendingTransition_.kind == PendingTransitionKind::None) {
    return false;
  }
  const PendingTransition transition = pendingTransition_;
  pendingTransition_ = {};
  pendingTransitionPopupRequested_ = false;
  executingPendingTransition_ = true;
  bool succeeded = false;
  switch (transition.kind) {
  case PendingTransitionKind::SwitchAuthoringMode:
    succeeded = SwitchAuthoringMode(scene, transition.mode, statusMessage_);
    break;
  case PendingTransitionKind::OpenModel:
    succeeded = OpenModel(scene, transition.assetGuid, statusMessage_);
    break;
  case PendingTransitionKind::OpenMaterial:
    succeeded = OpenMaterialAsset(scene, transition.assetGuid, statusMessage_);
    break;
  case PendingTransitionKind::SelectMaterialSlot:
    succeeded = SelectSlotForEditing(
        scene, transition.materialIndex, statusMessage_);
    break;
  case PendingTransitionKind::CreateMaterial:
    succeeded = CreateMaterial(scene, transition.templateId,
                               transition.instance, statusMessage_);
    break;
  case PendingTransitionKind::CreateAuxiliaryMaterial:
    succeeded = CreateAuxiliaryMaterialAsset(
        scene, transition.materialKind, statusMessage_);
    break;
  case PendingTransitionKind::OpenMaterialSet:
    succeeded = OpenMaterialSet(scene, transition.assetGuid, statusMessage_);
    break;
  case PendingTransitionKind::CreateMaterialSet:
    succeeded = CreateMaterialSet(scene, statusMessage_);
    break;
  case PendingTransitionKind::None:
    break;
  }
  executingPendingTransition_ = false;
  return succeeded;
}

void ModelMaterialWorkspaceController::DrawPendingTransitionModal(
    DocumentSceneBase &scene) {
#if defined(HIKARI_WITH_EDITOR)
  if (pendingTransition_.kind != PendingTransitionKind::None &&
      (pendingTransitionPopupRequested_ ||
       !ImGui::IsPopupOpen(
           "Unsaved Material Changes###ModelMaterial/PendingTransition"))) {
    ImGui::OpenPopup("Unsaved Material Changes###ModelMaterial/PendingTransition");
    pendingTransitionPopupRequested_ = false;
  }
  if (!ImGui::BeginPopupModal(
          "Unsaved Material Changes###ModelMaterial/PendingTransition",
          nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }

  const char *destination = "the requested destination";
  switch (pendingTransition_.kind) {
  case PendingTransitionKind::SwitchAuthoringMode:
    destination = "the other material workflow";
    break;
  case PendingTransitionKind::OpenModel:
    destination = "the selected model";
    break;
  case PendingTransitionKind::OpenMaterial:
    destination = "the selected material";
    break;
  case PendingTransitionKind::SelectMaterialSlot:
    destination = "the selected model part";
    break;
  case PendingTransitionKind::CreateMaterial:
  case PendingTransitionKind::CreateAuxiliaryMaterial:
    destination = "the new material document";
    break;
  case PendingTransitionKind::OpenMaterialSet:
  case PendingTransitionKind::CreateMaterialSet:
    destination = "the requested Material Set";
    break;
  case PendingTransitionKind::None:
    break;
  }
  ImGui::TextWrapped("Save changes before continuing to %s?", destination);
  if (authoringDocument_.IsMaterialDirty() ||
      authoringDocument_.IsAbPreviewActive()) {
    ImGui::BulletText("Material graph has unsaved changes");
  }
  if (authoringDocument_.IsBindingDirty()) {
    ImGui::BulletText("Model Material Set has unsaved changes");
  }
  ImGui::Spacing();

  if (ImGui::Button("Save and Continue", ImVec2(150.0f, 0.0f))) {
    if (SaveForPendingTransition(scene)) {
      (void)ExecutePendingTransition(scene);
      ImGui::CloseCurrentPopup();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Discard", ImVec2(100.0f, 0.0f))) {
    DiscardForPendingTransition(scene);
    (void)ExecutePendingTransition(scene);
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
    pendingTransition_ = {};
    pendingTransitionPopupRequested_ = false;
    statusMessage_ = "material navigation canceled; edits are unchanged";
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
#else
  (void)scene;
#endif
}

} // namespace HIKARI::EDITOR
