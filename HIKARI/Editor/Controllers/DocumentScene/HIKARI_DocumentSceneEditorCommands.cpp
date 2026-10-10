#include "Editor/Project/HIKARI_GameProjectEditorWorkspaceHost.h"
#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include "Core/Text/HIKARI_Localization.h"
#include <cstdint>
#include <span>
#include <utility>

#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/History/HIKARI_CinematicsHistoryCommand.h"
#include "Editor/History/HIKARI_SceneObjectsHistoryCommand.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Lighting/HIKARI_SceneLightingSet.h"

namespace HIKARI {

void DocumentSceneEditorController::SyncDocumentHistory(DocumentSceneBase& scene) {
    const bool dirty = context_.sceneDirty || scene.HasUnsavedSceneChanges();
    if (documentHistory_.SyncDocumentRevision(scene.GetSceneDocumentRevision(), dirty)) {
        historyExternalDirty_ = dirty;
        return;
    }

    if (!dirty && documentHistory_.IsDirty()) {
        documentHistory_.MarkSaved();
        historyExternalDirty_ = false;
    }
}

void DocumentSceneEditorController::ConfigureEditorCommands(DocumentSceneBase& scene) {
    using EDITOR::EditorCommandId;
    commandRouter_.BeginFrame();
    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Ui) && uiWorkspaceController_.Animation().HasFocus()) {
        uiWorkspaceController_.Animation().BindCommands(commandRouter_);
        return;
    }
    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Text)) {
        textWorkspaceController_.BindDocumentCommands(commandRouter_);
        return;
    }
    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::GameplayCamera)) {
        GameplayCameraWorkspace().BindDocumentCommands(commandRouter_);
        return;
    }
    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Project)) {
        if (projectWorkspaceHost_) {
            commandRouter_.Bind(EditorCommandId::SaveDocument, TEXT::Localize("menu.edit.save"),
                                projectWorkspaceHost_->Registry().HasActiveWorkspace(),
                                [this] { (void)projectWorkspaceHost_->Registry().RequestSaveActiveWorkspace(); });
        }
        return;
    }

    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::AnimationStateMachine)) {
        AnimationStateMachineWorkspace().BindDocumentCommands(commandRouter_, scene);
        return;
    }

    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::RuntimeBinding)) {
        RuntimeBindingWorkspace().BindDocumentCommands(commandRouter_);
        return;
    }

    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Vfx)) {
        VfxWorkspace().BindDocumentCommands(commandRouter_);
        return;
    }

    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Dialogue)) {
        DialogueWorkspace().BindDocumentCommands(commandRouter_, scene);
        return;
    }

    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::ModelCollision)) {
        ModelCollisionWorkspace().BindDocumentCommands(commandRouter_, scene);
        return;
    }

    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::ModelMaterial)) {
        ModelMaterialWorkspace().BindWorkspaceCommands(commandRouter_, scene);
        return;
    }

    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Cinematics) &&
        CinematicsWorkspace().IsEditingSequenceAsset()) {
        CinematicsWorkspace().BindDocumentCommands(commandRouter_, scene);
        return;
    }

    commandRouter_.BindOperation(EditorCommandId::SaveDocument, TEXT::Localize("menu.edit.saveAll"), true,
                                 "editor.save_all");
    // 焦点のある窓が資産文書を編集していれば、元に戻すとやり直しはその文書へ。それ以外は場面の履歴。
    if (EDITOR::EditorAssetDocument* focused = environmentPanel_.FocusedDocument();
        focused != nullptr && (focused->CanUndo() || focused->CanRedo())) {
        const std::string name = focused->DisplayName();
        commandRouter_.Bind(EditorCommandId::Undo, TEXT::Localize("menu.edit.undo") + " " + name, focused->CanUndo(),
                            [focused]() { (void)focused->Undo(); });
        commandRouter_.Bind(EditorCommandId::Redo, TEXT::Localize("menu.edit.redo") + " " + name, focused->CanRedo(),
                            [focused]() { (void)focused->Redo(); });
    } else {
        std::string undoLabel = TEXT::Localize("menu.edit.undo");
        if (const std::string* label = documentHistory_.GetUndoLabel()) {
            undoLabel += " " + *label;
        }
        std::string redoLabel = TEXT::Localize("menu.edit.redo");
        if (const std::string* label = documentHistory_.GetRedoLabel()) {
            redoLabel += " " + *label;
        }
        commandRouter_.Bind(EditorCommandId::Undo, std::move(undoLabel), documentHistory_.CanUndo(),
                            [this, &scene]() { ExecuteSceneDocumentHistory(scene, false); });
        commandRouter_.Bind(EditorCommandId::Redo, std::move(redoLabel), documentHistory_.CanRedo(),
                            [this, &scene]() { ExecuteSceneDocumentHistory(scene, true); });
    }

    if (!workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Scene) &&
        !workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::LookDev) &&
        !workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Ui) &&
        !workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Text)) {
        return;
    }
    commandRouter_.BindOperation(
        EditorCommandId::DuplicateSelection, TEXT::Localize("ui.command.duplicateSelection"),
        sceneObjectCommands_.CanExecute(EDITOR::SceneObjectCommandId::Duplicate, scene, context_),
        "scene.duplicate_object");
    commandRouter_.BindOperation(EditorCommandId::DeleteSelection, TEXT::Localize("ui.command.deleteSelection"),
                                 sceneObjectCommands_.CanExecute(EDITOR::SceneObjectCommandId::Delete, scene, context_),
                                 "scene.delete_object");
    commandRouter_.Bind(EditorCommandId::FocusSelection, "Focus Selection",
                        context_.selection.GetSelectedObjectCount() != 0u,
                        [this, &scene]() { FocusSceneObjects(scene, context_.selection.GetSelectedObjectIds()); });
}

std::vector<EDITOR::EditorAssetDocument*> DocumentSceneEditorController::UnsavedAssetDocuments() {
    std::vector<EDITOR::EditorAssetDocument*> documents;
    for (EDITOR::EditorAssetDocument* document : environmentPanel_.Documents()) {
        if (document->IsOpen() && document->IsDirty()) {
            documents.push_back(document);
        }
    }
    EDITOR::EditorAssetDocument& camera = GameplayCameraWorkspace().Document();
    if (camera.IsOpen() && camera.IsDirty()) {
        documents.push_back(&camera);
    }
    return documents;
}

std::string DocumentSceneEditorController::UnsavedDocumentSummary(const DocumentSceneBase& scene) {
    std::string summary;
    const auto add = [&summary](const std::string& name) {
        summary += (summary.empty() ? "" : TEXT::Localize("shell.listSeparator")) + name;
    };
    if (context_.sceneDirty || scene.HasUnsavedSceneChanges()) {
        add(scene.GetSceneDocument().sceneName);
    }
    for (const EDITOR::EditorAssetDocument* document : UnsavedAssetDocuments()) {
        add(document->DisplayName());
    }
    return summary;
}

bool DocumentSceneEditorController::SaveAllDocuments(DocumentSceneBase& scene, std::vector<std::string>& saved,
                                                     std::string& failures) {
    const auto fail = [&failures](const std::string& name, const std::string& message) {
        failures += (failures.empty() ? "" : "; ") + name + ": " + message;
    };
    if (SaveSceneDocument(scene)) {
        saved.push_back(scene.GetSceneDocument().sceneName);
    } else {
        fail(scene.GetSceneDocument().sceneName, viewportDropMessage_);
    }
    for (EDITOR::EditorAssetDocument* document : UnsavedAssetDocuments()) {
        std::string message;
        if (document->SaveDocument(scene.GetAssetDatabase(), message)) {
            saved.push_back(document->DisplayName());
        } else {
            fail(document->DisplayName(), message);
        }
    }
    if (CinematicsWorkspace().IsEditingSequenceAsset() && CinematicsWorkspace().IsSequenceDocumentDirty()) {
        std::string message;
        if (CinematicsWorkspace().SaveSequenceDocument(scene, message)) {
            saved.push_back("Sequence");
        } else {
            fail("Sequence", message);
        }
    }
    viewportDropMessage_ = failures.empty() ? TEXT::Localize("ui.save.allSaved") : failures;
    return failures.empty();
}

EDITOR::EditorOperationResult DocumentSceneEditorController::ExecuteSaveAll() {
    if (aiOperationScene_ == nullptr) {
        return EDITOR::OperationFailure("NO_SCENE", "No scene is open.");
    }
    std::vector<std::string> saved;
    std::string failures;
    if (!SaveAllDocuments(*aiOperationScene_, saved, failures)) {
        return EDITOR::OperationFailure("SAVE_FAILED", failures);
    }
    return EDITOR::OperationOk("Saved.", {{"saved", std::move(saved)}});
}

bool DocumentSceneEditorController::SaveSceneDocument(DocumentSceneBase& scene) {
    if (scene.SaveCurrentSceneDocument()) {
        documentHistory_.MarkSaved();
        historyExternalDirty_ = false;
        context_.sceneDirty = false;
        scene.SetUnsavedSceneChanges(false);
        viewportDropMessage_ = "Scene saved";
        return true;
    } else {
        viewportDropMessage_ = "Save failed; save the scene as an asset first";
        return false;
    }
}

void DocumentSceneEditorController::ExecuteSceneDocumentHistory(DocumentSceneBase& scene, bool redo) {
    ApplyHistoryResult(
        scene, redo ? documentHistory_.Redo(scene.GetSceneDocument()) : documentHistory_.Undo(scene.GetSceneDocument()),
        redo);
}

void DocumentSceneEditorController::ApplyHistoryResult(DocumentSceneBase& scene,
                                                       const EDITOR::EditorHistoryResult& result, bool redo) {
    if (!result.changed) {
        viewportDropMessage_ = redo ? "Nothing to redo" : "Nothing to undo";
        return;
    }
    ApplyDocumentImpact(scene, result.impact, result.objectChanges ? &*result.objectChanges : nullptr);

    const bool dirty = historyExternalDirty_ || documentHistory_.IsDirty();
    context_.sceneDirty = dirty;
    scene.SetUnsavedSceneChanges(dirty);
    viewportDropMessage_ = std::string(redo ? "Redo: " : "Undo: ") + result.label;
}

void DocumentSceneEditorController::ApplyDocumentImpact(DocumentSceneBase& scene, EDITOR::EditorDocumentImpact impact,
                                                        const SceneObjectChanges* objectChanges) {
    if (EDITOR::HasEditorDocumentImpact(impact, EDITOR::EditorDocumentImpact::Cinematics)) {
        CinematicsWorkspace().OnCinematicsDocumentRestored(scene, workspaceHost_);
    }
    if (EDITOR::HasEditorDocumentImpact(impact, EDITOR::EditorDocumentImpact::Systems)) {
        sceneAuthoringUtilityWindows_.SetSystemsRuntimeApplyStatus(scene.ApplySystemRuntimeChanges());
    }
    if (EDITOR::HasEditorDocumentImpact(impact, EDITOR::EditorDocumentImpact::RuntimeWorld)) {
        if (objectChanges != nullptr) {
            selectionSync_.ApplyRuntimeObjectChangesWithSelectionSync(scene, context_.selection,
                                                                      context_.nextSceneObjectId, *objectChanges);
        } else {
            selectionSync_.RebuildRuntimeWorldWithSelectionSync(scene, context_.selection, context_.nextSceneObjectId);
        }
    }
    if (EDITOR::HasEditorDocumentImpact(impact, EDITOR::EditorDocumentImpact::Environment)) {
        if (!scene.ApplyDocumentEnvironmentRuntimeChanges()) {
            viewportDropMessage_ = "Environment runtime refresh failed";
        }
    }
    if (EDITOR::HasEditorDocumentImpact(impact, EDITOR::EditorDocumentImpact::Lighting)) {
        if (!scene.RefreshLightingRuntime()) {
            viewportDropMessage_ = "Lighting runtime refresh failed";
        }
    }
}

void DocumentSceneEditorController::CommitAppliedDocumentCommand(
    DocumentSceneBase& scene, std::unique_ptr<EDITOR::IEditorDocumentCommand> command, uint64_t mergeGroup,
    bool externalDirtyBefore, EDITOR::EditorDocumentImpact immediateImpact) {
    if (!command) {
        return;
    }
    const std::optional<SceneObjectChanges> objectChanges = command->GetObjectChanges(false);
    historyExternalDirty_ |= externalDirtyBefore;
    documentHistory_.RecordApplied(std::move(command), mergeGroup);
    context_.sceneDirty = true;
    scene.SetUnsavedSceneChanges(true);
    ApplyDocumentImpact(scene, immediateImpact, objectChanges ? &*objectChanges : nullptr);
}

void DocumentSceneEditorController::CommitSceneObjectAuthoringHistory(DocumentSceneBase& scene,
                                                                      SceneObjectAuthoringHistoryRequest history) {
    EDITOR::EditorDocumentImpact impact =
        history.runtimeWorldAffected ? EDITOR::EditorDocumentImpact::RuntimeWorld : EDITOR::EditorDocumentImpact::None;
    const auto beforeLighting =
        LIGHTING_AUTHORING::BuildSceneLightingSet(std::span<const SceneObjectData>(history.beforeObjects));
    const auto afterLighting =
        LIGHTING_AUTHORING::BuildSceneLightingSet(std::span<const SceneObjectData>(history.afterObjects));
    const bool lightingChanged = !LIGHTING_AUTHORING::AreSceneLightingSetsEquivalent(beforeLighting, afterLighting);
    if (lightingChanged) {
        impact = impact | EDITOR::EditorDocumentImpact::Lighting;
    }
    EDITOR::EditorDocumentImpact immediateImpact = history.runtimeWorldRefreshRequired
                                                       ? EDITOR::EditorDocumentImpact::RuntimeWorld
                                                       : EDITOR::EditorDocumentImpact::None;
    if (lightingChanged) {
        immediateImpact = immediateImpact | EDITOR::EditorDocumentImpact::Lighting;
    }
    CommitAppliedDocumentCommand(
        scene,
        EDITOR::MakeSceneObjectsHistoryCommand(std::move(history.label), std::move(history.beforeObjects),
                                               std::move(history.afterObjects), std::move(history.beforeCamera),
                                               std::move(history.afterCamera), impact),
        history.mergeGroup, history.dirtyBefore && !documentHistory_.IsDirty(), immediateImpact);
}

void DocumentSceneEditorController::RecordCinematicsHistory(DocumentSceneBase& scene, SceneCinematicsSettings before,
                                                            uint64_t mergeGroup, bool externalDirtyBefore) {
    CommitAppliedDocumentCommand(scene,
                                 EDITOR::MakeCinematicsHistoryCommand("Edit Camera Timeline", std::move(before),
                                                                      scene.GetSceneDocument().cinematics),
                                 mergeGroup, externalDirtyBefore);
}

} // namespace HIKARI
