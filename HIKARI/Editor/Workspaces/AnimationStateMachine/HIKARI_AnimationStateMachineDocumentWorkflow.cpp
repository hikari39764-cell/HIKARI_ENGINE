#include "Editor/Workspaces/AnimationStateMachine/HIKARI_AnimationStateMachineWorkspaceController.h"

#include <utility>

#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif
//  アニメーションステートマシンのワークスペースコントローラーの実装を提供します。
//  ワークスペースコントローラーは、ドキュメントの状態管理、選択状態の管理、ドキュメントアクションのリクエストと実行、未保存のドキュメントダイアログの描画などを担当します。
namespace HIKARI::EDITOR {
namespace {
	//  未保存のドキュメントダイアログのタイトル
constexpr const char *kUnsavedDocumentPopup = "Unsaved Animation State Machine";
}
//  エントリーステートを選択する関数
void AnimationStateMachineWorkspaceController::SelectEntryState() noexcept {
  selectionKind_ = SelectionKind::State;
  selectedStateId_ = document_.Definition().entryStateId;
  selectedParameterId_ = {};
  selectedTransitionId_ = {};
  NormalizeSelection();
}
//  ドキュメントアクションをリクエストする関数
void AnimationStateMachineWorkspaceController::RequestDocumentAction(
    DocumentSceneBase &scene, AnimationStateMachineWorkspaceResult &result,
    GraphMenuRequests &requests, PendingDocumentAction action,
    AssetGuid assetGuid) {
  if (action == PendingDocumentAction::None)
    return;
  if (!document_.IsDirty()) {
    ExecuteDocumentAction(scene, result, requests, action, assetGuid);
    return;
  }

  pendingDocumentAction_ = action;
  pendingDocumentAssetGuid_ = std::move(assetGuid);
  requestUnsavedDocumentDialog_ = true;
}
//  ドキュメントアクションを実行する関数
void AnimationStateMachineWorkspaceController::ExecuteDocumentAction(
    DocumentSceneBase &scene, AnimationStateMachineWorkspaceResult &result,
    GraphMenuRequests &requests, PendingDocumentAction action,
    const AssetGuid &assetGuid) {
  switch (action) {
	  // 新しいドキュメントを作成する場合
  case PendingDocumentAction::NewDocument:
    document_.New();
    SelectEntryState();
    requests.resetView = true;
    statusMessage_ = "New animation state machine";
    break;
	// 既存のアセットを開く場合
  case PendingDocumentAction::OpenAsset:
    if (assetGuid.IsValid() &&
        document_.Open(scene.GetAssetDatabase(), assetGuid, statusMessage_)) {
      SelectEntryState();
      requests.frameAll = true;
    }
    break;
	// シーンに戻る場合
  case PendingDocumentAction::ExitToScene:
    result.exitToSceneRequested = true;
    break;
  case PendingDocumentAction::None:
    break;
  }
  // ドキュメントアクションの状態をリセットする
  pendingDocumentAction_ = PendingDocumentAction::None;
  pendingDocumentAssetGuid_ = {};
}
//  未保存のドキュメントダイアログを描画する関数
void AnimationStateMachineWorkspaceController::DrawUnsavedDocumentDialog(
    DocumentSceneBase &scene, AnimationStateMachineWorkspaceResult &result,
    GraphMenuRequests &requests) {
#if defined(HIKARI_WITH_EDITOR)
  if (requestUnsavedDocumentDialog_) {
    ImGui::OpenPopup(kUnsavedDocumentPopup);
    requestUnsavedDocumentDialog_ = false;
  }
  // 未保存のドキュメントダイアログを描画する
  if (!ImGui::BeginPopupModal(kUnsavedDocumentPopup, nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  // ダイアログの内容を描画する
  ImGui::TextUnformatted("The current state machine has unsaved changes.");
  ImGui::TextDisabled("Save them before continuing, or discard them.");
  ImGui::Spacing();

  if (ImGui::Button("Save and Continue")) {
    if (Save(scene, statusMessage_)) {
      ExecuteDocumentAction(scene, result, requests, pendingDocumentAction_,
                            pendingDocumentAssetGuid_);
      ImGui::CloseCurrentPopup();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Discard and Continue")) {
    ExecuteDocumentAction(scene, result, requests, pendingDocumentAction_,
                          pendingDocumentAssetGuid_);
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    pendingDocumentAction_ = PendingDocumentAction::None;
    pendingDocumentAssetGuid_ = {};
    statusMessage_ = "Document action cancelled";
    ImGui::CloseCurrentPopup();
  }

  ImGui::EndPopup();
#else
  (void)scene;
  (void)result;
  (void)requests;
#endif
}

} // namespace HIKARI::EDITOR
