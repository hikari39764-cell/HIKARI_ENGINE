#include "Editor/Workspaces/AnimationStateMachine/HIKARI_AnimationStateMachineWorkspaceController.h"

#include <cstdio>
#include <string>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
//  グラフメニューバーを描画する関数
void AnimationStateMachineWorkspaceController::DrawGraphMenuBar(DocumentSceneBase& scene,
                                                                AnimationStateMachineWorkspaceResult& result,
                                                                GraphMenuRequests& requests,
                                                                EditorCommandRouter& commandRouter) {
#if defined(HIKARI_WITH_EDITOR)
    if (!ImGui::BeginMenuBar())
        return;

    if (ImGui::BeginMenu(TEXT::Localize("ui.asm.menu.file").c_str())) {
        if (ImGui::MenuItem(TEXT::Localize("ui.asm.menu.new").c_str())) {
            RequestDocumentAction(scene, result, requests, PendingDocumentAction::NewDocument);
        }
        if (ImGui::BeginMenu(TEXT::Localize("ui.asm.menu.openAsset").c_str())) {
            const auto assets = scene.GetAssetDatabase().CollectByType(AssetType::AnimationStateMachine);
            if (assets.empty()) {
                ImGui::TextDisabled("%s", TEXT::Localize("ui.asm.menu.noAssets").c_str());
            }
            for (const AssetRecord* record : assets) {
                if (record == nullptr)
                    continue;
                ImGui::PushID(record->guid.value.c_str());
                const bool selected = record->guid == document_.GetAssetGuid();
                if (ImGui::MenuItem(record->displayName.c_str(), nullptr, selected)) {
                    RequestDocumentAction(scene, result, requests, PendingDocumentAction::OpenAsset, record->guid);
                }
                ImGui::PopID();
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(TEXT::Localize("ui.asm.menu.save").c_str(),
                            EditorCommandRouter::Shortcut(EditorCommandId::SaveDocument), false,
                            commandRouter.CanExecute(EditorCommandId::SaveDocument))) {
            (void)commandRouter.Execute(EditorCommandId::SaveDocument);
        }
        ImGui::BeginDisabled(!document_.GetAssetGuid().IsValid());
        if (ImGui::MenuItem(TEXT::Localize("ui.asm.menu.revert").c_str())) {
            (void)document_.Revert(scene.GetAssetDatabase(), statusMessage_);
            NormalizeSelection();
            requests.frameAll = true;
        }
        ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::MenuItem(TEXT::Localize("ui.asm.menu.exitToScene").c_str())) {
            RequestDocumentAction(scene, result, requests, PendingDocumentAction::ExitToScene);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(TEXT::Localize("ui.asm.menu.edit").c_str())) {
        ImGui::BeginDisabled(!commandRouter.CanExecute(EditorCommandId::Undo));
        if (ImGui::MenuItem(TEXT::Localize("ui.asm.menu.undo").c_str(),
                            EditorCommandRouter::Shortcut(EditorCommandId::Undo))) {
            (void)commandRouter.Execute(EditorCommandId::Undo);
        }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!commandRouter.CanExecute(EditorCommandId::Redo));
        if (ImGui::MenuItem(TEXT::Localize("ui.asm.menu.redo").c_str(),
                            EditorCommandRouter::Shortcut(EditorCommandId::Redo))) {
            (void)commandRouter.Execute(EditorCommandId::Redo);
        }
        ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::MenuItem(TEXT::Localize("ui.asm.menu.addState").c_str())) {
            requests.addStateAtCenter = true;
        }
        ImGui::BeginDisabled(document_.Definition().states.size() < 2u);
        if (ImGui::MenuItem(TEXT::Localize("ui.asm.menu.addTransition").c_str()))
            AddTransition();
        ImGui::EndDisabled();
        const bool canDelete = selectionKind_ == SelectionKind::Transition ||
                               selectionKind_ == SelectionKind::Parameter ||
                               (selectionKind_ == SelectionKind::State && document_.Definition().states.size() > 1u);
        ImGui::BeginDisabled(!canDelete);
        if (ImGui::MenuItem(TEXT::Localize("ui.asm.menu.deleteSelection").c_str(), "Delete")) {
            if (operationRegistry_ != nullptr) {
                const EditorOperationResult operationResult = operationRegistry_->Execute("asm.delete_selected");
                statusMessage_ = operationResult.message;
            }
        }
        ImGui::EndDisabled();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(TEXT::Localize("ui.asm.menu.view").c_str())) {
        if (ImGui::MenuItem(TEXT::Localize("ui.asm.menu.frameSelection").c_str(), "F")) {
            requests.frameSelection = true;
        }
        if (ImGui::MenuItem(TEXT::Localize("ui.asm.menu.frameAll").c_str(), "Home")) {
            requests.frameAll = true;
        }
        if (ImGui::MenuItem(TEXT::Localize("ui.asm.menu.resetView").c_str(), "1")) {
            requests.resetView = true;
        }
        ImGui::Separator();
        char zoomBuffer[64] = {};
        std::snprintf(zoomBuffer, sizeof(zoomBuffer), TEXT::Localize("ui.asm.menu.zoom").c_str(), graphZoom_ * 100.0f);
        ImGui::TextDisabled("%s", zoomBuffer);
        ImGui::EndMenu();
    }

    constexpr ImVec2 kQuickActionSize{22.0f, 22.0f};
    ImGui::SameLine(0.0f, 8.0f);
    ToolbarDivider(18.0f);
    ImGui::SameLine(0.0f, 6.0f);
    if (IconButton(EditorGlyph::NewDocument, "AnimationSmNew", EditorButtonTone::Quiet, kQuickActionSize,
                   TEXT::Localize("ui.asm.menu.newTooltip").c_str())) {
        RequestDocumentAction(scene, result, requests, PendingDocumentAction::NewDocument);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!commandRouter.CanExecute(EditorCommandId::SaveDocument));
    if (IconButton(EditorGlyph::Save, "AnimationSmSave",
                   document_.IsDirty() ? EditorButtonTone::Primary : EditorButtonTone::Quiet, kQuickActionSize,
                   TEXT::Localize("ui.asm.menu.saveTooltip").c_str())) {
        (void)commandRouter.Execute(EditorCommandId::SaveDocument);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!commandRouter.CanExecute(EditorCommandId::Undo));
    if (IconButton(EditorGlyph::Undo, "AnimationSmUndo", EditorButtonTone::Quiet, kQuickActionSize,
                   TEXT::Localize("ui.asm.menu.undoTooltip").c_str())) {
        (void)commandRouter.Execute(EditorCommandId::Undo);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!commandRouter.CanExecute(EditorCommandId::Redo));
    if (IconButton(EditorGlyph::Redo, "AnimationSmRedo", EditorButtonTone::Quiet, kQuickActionSize,
                   TEXT::Localize("ui.asm.menu.redoTooltip").c_str())) {
        (void)commandRouter.Execute(EditorCommandId::Redo);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (IconButton(EditorGlyph::Focus, "AnimationSmFrameAll", EditorButtonTone::Quiet, kQuickActionSize,
                   TEXT::Localize("ui.asm.menu.frameAllTooltip").c_str())) {
        requests.frameAll = true;
    }

    const std::string documentLabel = document_.Definition().name + (document_.IsDirty() ? " *" : "");
    const float labelWidth = ImGui::CalcTextSize(documentLabel.c_str()).x;
    const float rightEdge = ImGui::GetWindowWidth() - labelWidth - ImGui::GetStyle().WindowPadding.x;
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
