#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include <cstdint>
#include <utility>

#include "Editor/History/HIKARI_CinematicsHistoryCommand.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Lighting/HIKARI_SceneLightingSet.h"

namespace HIKARI {

    void DocumentSceneEditorController::SyncDocumentHistory(
        DocumentSceneBase& scene) {

        const bool dirty =
            context_.sceneDirty || scene.HasUnsavedSceneChanges();
        if (documentHistory_.SyncDocumentRevision(
                scene.GetSceneDocumentRevision(),
                dirty)) {
            historyExternalDirty_ = dirty;
            return;
        }

        if (!dirty && documentHistory_.IsDirty()) {
            documentHistory_.MarkSaved();
            historyExternalDirty_ = false;
        }
    }

    void DocumentSceneEditorController::ConfigureEditorCommands(
        DocumentSceneBase& scene) {
        using EDITOR::EditorCommandId;
        commandRouter_.BeginFrame();

        if (workspaceHost_.IsActive(
                EDITOR::EditorWorkspaceId::AnimationStateMachine)) {
            animationStateMachineWorkspaceController_.BindDocumentCommands(
                commandRouter_,
                scene);
            return;
        }

        if (workspaceHost_.IsActive(
                EDITOR::EditorWorkspaceId::ModelCollision)) {
            modelCollisionWorkspaceController_.BindDocumentCommands(
                commandRouter_,
                scene);
            return;
        }

        if (workspaceHost_.IsActive(
                EDITOR::EditorWorkspaceId::ModelMaterial)) {
            modelMaterialWorkspaceController_.BindWorkspaceCommands(
                commandRouter_, scene);
            return;
        }

        if (workspaceHost_.IsActive(
                EDITOR::EditorWorkspaceId::Cinematics) &&
            cinematicsWorkspaceController_.IsEditingSequenceAsset()) {
            cinematicsWorkspaceController_.BindDocumentCommands(
                commandRouter_,
                scene);
            return;
        }

        std::string undoLabel = "Undo Scene Edit";
        if (const std::string* label = documentHistory_.GetUndoLabel()) {
            undoLabel = "Undo " + *label;
        }
        std::string redoLabel = "Redo Scene Edit";
        if (const std::string* label = documentHistory_.GetRedoLabel()) {
            redoLabel = "Redo " + *label;
        }
        commandRouter_.Bind(
            EditorCommandId::SaveDocument,
            "Save Scene",
            true,
            [this, &scene]() { SaveSceneDocument(scene); });
        commandRouter_.Bind(
            EditorCommandId::Undo,
            std::move(undoLabel),
            documentHistory_.CanUndo(),
            [this, &scene]() {
                ExecuteSceneDocumentHistory(scene, false);
            });
        commandRouter_.Bind(
            EditorCommandId::Redo,
            std::move(redoLabel),
            documentHistory_.CanRedo(),
            [this, &scene]() {
                ExecuteSceneDocumentHistory(scene, true);
            });

        if (!workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Scene)) {
            return;
        }
        commandRouter_.Bind(
            EditorCommandId::DuplicateSelection,
            "Duplicate Selection",
            sceneObjectCommands_.CanExecute(
                EDITOR::SceneObjectCommandId::Duplicate,
                scene,
                context_),
            [this, &scene]() {
                (void)sceneObjectCommands_.Execute(
                    EDITOR::SceneObjectCommandId::Duplicate,
                    scene,
                    context_,
                    selectionSync_);
            });
        commandRouter_.Bind(
            EditorCommandId::DeleteSelection,
            "Delete Selection",
            sceneObjectCommands_.CanExecute(
                EDITOR::SceneObjectCommandId::Delete,
                scene,
                context_),
            [this, &scene]() {
                (void)sceneObjectCommands_.Execute(
                    EDITOR::SceneObjectCommandId::Delete,
                    scene,
                    context_,
                    selectionSync_);
            });
        commandRouter_.Bind(
            EditorCommandId::FocusSelection,
            "Focus Selection",
            context_.selection.GetSelectedObjectCount() != 0u,
            [this, &scene]() {
                FocusSceneObjects(
                    scene,
                    context_.selection.GetSelectedObjectIds());
            });
    }

    void DocumentSceneEditorController::SaveSceneDocument(
        DocumentSceneBase& scene) {

        scene.GetSceneDocument().environment = scene.GetSceneEnvironment();
        if (scene.SaveCurrentSceneDocument()) {
            documentHistory_.MarkSaved();
            historyExternalDirty_ = false;
            context_.sceneDirty = false;
            scene.SetUnsavedSceneChanges(false);
            viewportDropMessage_ = "Scene saved";
        } else {
            viewportDropMessage_ =
                "Save failed; save the scene as an asset first";
        }
    }

    void DocumentSceneEditorController::ExecuteSceneDocumentHistory(
        DocumentSceneBase& scene,
        bool redo) {

        ApplyHistoryResult(
            scene,
            redo
                ? documentHistory_.Redo(scene.GetSceneDocument())
                : documentHistory_.Undo(scene.GetSceneDocument()),
            redo);
    }

    void DocumentSceneEditorController::ApplyHistoryResult(
        DocumentSceneBase& scene,
        const EDITOR::EditorHistoryResult& result,
        bool redo) {

        if (!result.changed) {
            viewportDropMessage_ = redo
                ? "Nothing to redo"
                : "Nothing to undo";
            return;
        }
        ApplyDocumentImpact(scene, result.impact);

        const bool dirty =
            historyExternalDirty_ || documentHistory_.IsDirty();
        context_.sceneDirty = dirty;
        scene.SetUnsavedSceneChanges(dirty);
        viewportDropMessage_ = std::string(redo ? "Redo: " : "Undo: ") +
            result.label;
    }

    void DocumentSceneEditorController::ApplyDocumentImpact(
        DocumentSceneBase& scene,
        EDITOR::EditorDocumentImpact impact) {

        if (EDITOR::HasEditorDocumentImpact(
                impact,
                EDITOR::EditorDocumentImpact::Cinematics)) {
            cinematicsWorkspaceController_.OnCinematicsDocumentRestored(
                scene,
                workspaceHost_);
        }
        if (EDITOR::HasEditorDocumentImpact(
                impact,
                EDITOR::EditorDocumentImpact::Systems)) {
            sceneAuthoringUtilityWindows_.SetSystemsRuntimeApplyStatus(
                scene.ApplySystemRuntimeChanges());
        }
        if (EDITOR::HasEditorDocumentImpact(
                impact,
                EDITOR::EditorDocumentImpact::RuntimeWorld)) {
            selectionSync_.RebuildRuntimeWorldWithSelectionSync(
                scene,
                context_.selection,
                context_.nextSceneObjectId);
        }
        if (EDITOR::HasEditorDocumentImpact(
                impact,
                EDITOR::EditorDocumentImpact::Environment)) {
            if (!scene.ApplyDocumentEnvironmentRuntimeChanges()) {
                viewportDropMessage_ =
                    "Environment runtime refresh failed";
            }
        }
        if (EDITOR::HasEditorDocumentImpact(
                impact,
                EDITOR::EditorDocumentImpact::Lighting)) {
            if (!scene.RefreshLightingRuntime()) {
                viewportDropMessage_ =
                    "Lighting runtime refresh failed";
            }
        }
    }

    void DocumentSceneEditorController::CommitAppliedDocumentCommand(
        DocumentSceneBase& scene,
        std::unique_ptr<EDITOR::IEditorDocumentCommand> command,
        uint64_t mergeGroup,
        bool externalDirtyBefore,
        EDITOR::EditorDocumentImpact immediateImpact) {

        if (!command) {
            return;
        }
        historyExternalDirty_ |= externalDirtyBefore;
        documentHistory_.RecordApplied(
            std::move(command),
            mergeGroup);
        context_.sceneDirty = true;
        scene.SetUnsavedSceneChanges(true);
        ApplyDocumentImpact(scene, immediateImpact);
    }

    void DocumentSceneEditorController::RecordCinematicsHistory(
        DocumentSceneBase& scene,
        SceneCinematicsSettings before,
        uint64_t mergeGroup,
        bool externalDirtyBefore) {

        CommitAppliedDocumentCommand(
            scene,
            EDITOR::MakeCinematicsHistoryCommand(
                "Edit Camera Timeline",
                std::move(before),
                scene.GetSceneDocument().cinematics),
            mergeGroup,
            externalDirtyBefore);
    }

} // namespace HIKARI
