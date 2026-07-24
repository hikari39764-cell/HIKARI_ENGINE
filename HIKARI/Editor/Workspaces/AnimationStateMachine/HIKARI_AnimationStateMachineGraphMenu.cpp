#include "Editor/Workspaces/AnimationStateMachine/HIKARI_AnimationStateMachineWorkspaceController.h"

#include <string>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
	//  グラフメニューバーを描画する関数
void AnimationStateMachineWorkspaceController::DrawGraphMenuBar(
    DocumentSceneBase &scene, AnimationStateMachineWorkspaceResult &result,
    GraphMenuRequests &requests, EditorCommandRouter &commandRouter) {
#if defined(HIKARI_WITH_EDITOR)
  if (!ImGui::BeginMenuBar())
    return;

  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("New State Machine")) {
      RequestDocumentAction(scene, result, requests,
                            PendingDocumentAction::NewDocument);
    }
    if (ImGui::BeginMenu("Open Asset")) {
      const auto assets = scene.GetAssetDatabase().CollectByType(
          AssetType::AnimationStateMachine);
      if (assets.empty()) {
        ImGui::TextDisabled("No state machine assets");
      }
      for (const AssetRecord *record : assets) {
        if (record == nullptr)
          continue;
        ImGui::PushID(record->guid.value.c_str());
        const bool selected = record->guid == document_.GetAssetGuid();
        if (ImGui::MenuItem(record->displayName.c_str(), nullptr, selected)) {
          RequestDocumentAction(scene, result, requests,
                                PendingDocumentAction::OpenAsset, record->guid);
        }
        ImGui::PopID();
      }
      ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem(
            "Save",
            EditorCommandRouter::Shortcut(EditorCommandId::SaveDocument), false,
            commandRouter.CanExecute(EditorCommandId::SaveDocument))) {
      (void)commandRouter.Execute(EditorCommandId::SaveDocument);
    }
    ImGui::BeginDisabled(!document_.GetAssetGuid().IsValid());
    if (ImGui::MenuItem("Revert Saved Asset")) {
      (void)document_.Revert(scene.GetAssetDatabase(), statusMessage_);
      NormalizeSelection();
      requests.frameAll = true;
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    if (ImGui::MenuItem("Exit to Scene")) {
      RequestDocumentAction(scene, result, requests,
                            PendingDocumentAction::ExitToScene);
    }
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Edit")) {
    ImGui::BeginDisabled(!commandRouter.CanExecute(EditorCommandId::Undo));
    if (ImGui::MenuItem("Undo",
                        EditorCommandRouter::Shortcut(EditorCommandId::Undo))) {
      (void)commandRouter.Execute(EditorCommandId::Undo);
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!commandRouter.CanExecute(EditorCommandId::Redo));
    if (ImGui::MenuItem("Redo",
                        EditorCommandRouter::Shortcut(EditorCommandId::Redo))) {
      (void)commandRouter.Execute(EditorCommandId::Redo);
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    if (ImGui::MenuItem("Add State")) {
      requests.addStateAtCenter = true;
    }
    ImGui::BeginDisabled(document_.Definition().states.size() < 2u);
    if (ImGui::MenuItem("Add Transition"))
      AddTransition();
    ImGui::EndDisabled();
    const bool canDelete = selectionKind_ == SelectionKind::Transition ||
                           selectionKind_ == SelectionKind::Parameter ||
                           (selectionKind_ == SelectionKind::State &&
                            document_.Definition().states.size() > 1u);
    ImGui::BeginDisabled(!canDelete);
    if (ImGui::MenuItem("Delete Selection", "Delete")) {
      if (selectionKind_ == SelectionKind::State) {
        DeleteSelectedState();
      } else if (selectionKind_ == SelectionKind::Parameter) {
        DeleteSelectedParameter();
      } else if (selectionKind_ == SelectionKind::Transition) {
        DeleteSelectedTransition();
      }
    }
    ImGui::EndDisabled();
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("View")) {
    if (ImGui::MenuItem("Frame Selection", "F")) {
      requests.frameSelection = true;
    }
    if (ImGui::MenuItem("Frame All", "Home")) {
      requests.frameAll = true;
    }
    if (ImGui::MenuItem("Reset View", "1")) {
      requests.resetView = true;
    }
    ImGui::Separator();
    ImGui::TextDisabled("Zoom: %.0f%%", graphZoom_ * 100.0f);
    ImGui::EndMenu();
  }

  constexpr ImVec2 kQuickActionSize{22.0f, 22.0f};
  ImGui::SameLine(0.0f, 8.0f);
  ToolbarDivider(18.0f);
  ImGui::SameLine(0.0f, 6.0f);
  if (IconButton(EditorGlyph::NewDocument, "AnimationSmNew",
                 EditorButtonTone::Quiet, kQuickActionSize,
                 "New state machine")) {
    RequestDocumentAction(scene, result, requests,
                          PendingDocumentAction::NewDocument);
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(
      !commandRouter.CanExecute(EditorCommandId::SaveDocument));
  if (IconButton(EditorGlyph::Save, "AnimationSmSave",
                 document_.IsDirty() ? EditorButtonTone::Primary
                                     : EditorButtonTone::Quiet,
                 kQuickActionSize, "Save state machine (Ctrl+S)")) {
    (void)commandRouter.Execute(EditorCommandId::SaveDocument);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!commandRouter.CanExecute(EditorCommandId::Undo));
  if (IconButton(EditorGlyph::Undo, "AnimationSmUndo", EditorButtonTone::Quiet,
                 kQuickActionSize, "Undo (Ctrl+Z)")) {
    (void)commandRouter.Execute(EditorCommandId::Undo);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!commandRouter.CanExecute(EditorCommandId::Redo));
  if (IconButton(EditorGlyph::Redo, "AnimationSmRedo", EditorButtonTone::Quiet,
                 kQuickActionSize, "Redo (Ctrl+Y)")) {
    (void)commandRouter.Execute(EditorCommandId::Redo);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (IconButton(EditorGlyph::Focus, "AnimationSmFrameAll",
                 EditorButtonTone::Quiet, kQuickActionSize,
                 "Frame all states (Home)")) {
    requests.frameAll = true;
  }

  const std::string documentLabel =
      document_.Definition().name + (document_.IsDirty() ? " *" : "");
  const float labelWidth = ImGui::CalcTextSize(documentLabel.c_str()).x;
  const float rightEdge =
      ImGui::GetWindowWidth() - labelWidth - ImGui::GetStyle().WindowPadding.x;
  if (rightEdge > ImGui::GetCursorPosX() + 24.0f) {
    ImGui::SetCursorPosX(rightEdge);
    ImGui::TextDisabled("%s", documentLabel.c_str());
  }
  ImGui::EndMenuBar();
  DrawUnsavedDocumentDialog(scene, result, requests);
#else
  (void)scene;
  (void)result;
  (void)requests;
#endif
}

} // namespace HIKARI::EDITOR
