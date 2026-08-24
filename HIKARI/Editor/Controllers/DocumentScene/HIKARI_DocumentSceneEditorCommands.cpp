#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include "Core/Text/HIKARI_Localization.h"
#include <cstdint>
#include <span>
#include <utility>

#include "Editor/History/HIKARI_CinematicsHistoryCommand.h"
#include "Editor/History/HIKARI_SceneObjectsHistoryCommand.h"
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
                EDITOR::EditorWorkspaceId::RuntimeBinding)) {
            runtimeBindingWorkspaceController_.BindDocumentCommands(
                commandRouter_);
            return;
        }

        if (workspaceHost_.IsActive(
                EDITOR::EditorWorkspaceId::Vfx)) {
            vfxWorkspaceController_.BindDocumentCommands(commandRouter_);
            return;
        }

        if (workspaceHost_.IsActive(
                EDITOR::EditorWorkspaceId::Dialogue)) {
            dialogueWorkspaceController_.BindDocumentCommands(
                commandRouter_, scene);
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
        commandRouter_.BindOperation(
            EditorCommandId::SaveDocument,
            TEXT::Localize("menu.edit.save"),
            true,
            "scene.save");
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
        commandRouter_.BindOperation(
            EditorCommandId::DuplicateSelection,
            TEXT::Localize("ui.command.duplicateSelection"),
            sceneObjectCommands_.CanExecute(
                EDITOR::SceneObjectCommandId::Duplicate,
                scene,
                context_),
            "scene.duplicate_object");
        commandRouter_.BindOperation(
            EditorCommandId::DeleteSelection,
            TEXT::Localize("ui.command.deleteSelection"),
            sceneObjectCommands_.CanExecute(
                EDITOR::SceneObjectCommandId::Delete,
                scene,
                context_),
            "scene.delete_object");
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

    bool DocumentSceneEditorController::SaveSceneDocument(
        DocumentSceneBase& scene) {

        scene.GetSceneDocument().environment = scene.GetSceneEnvironment();
        if (scene.SaveCurrentSceneDocument()) {
            documentHistory_.MarkSaved();
            historyExternalDirty_ = false;
            context_.sceneDirty = false;
            scene.SetUnsavedSceneChanges(false);
            viewportDropMessage_ = "Scene saved";
            return true;
        } else {
            viewportDropMessage_ =
                "Save failed; save the scene as an asset first";
            return false;
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

    void DocumentSceneEditorController::CommitSceneObjectAuthoringHistory(
        DocumentSceneBase& scene,
        SceneObjectAuthoringHistoryRequest history) {

        EDITOR::EditorDocumentImpact impact =
            history.runtimeWorldAffected
                ? EDITOR::EditorDocumentImpact::RuntimeWorld
                : EDITOR::EditorDocumentImpact::None;
        const auto beforeLighting =
            LIGHTING_AUTHORING::BuildSceneLightingSet(
                std::span<const SceneObjectData>(
                    history.beforeObjects));
        const auto afterLighting =
            LIGHTING_AUTHORING::BuildSceneLightingSet(
                std::span<const SceneObjectData>(
                    history.afterObjects));
        const bool lightingChanged =
            !LIGHTING_AUTHORING::AreSceneLightingSetsEquivalent(
                beforeLighting,
                afterLighting);
        if (lightingChanged) {
            impact = impact |
                EDITOR::EditorDocumentImpact::Lighting;
        }
        EDITOR::EditorDocumentImpact immediateImpact =
            history.runtimeWorldRefreshRequired
                ? EDITOR::EditorDocumentImpact::RuntimeWorld
                : EDITOR::EditorDocumentImpact::None;
        if (lightingChanged) {
            immediateImpact = immediateImpact |
                EDITOR::EditorDocumentImpact::Lighting;
        }
        CommitAppliedDocumentCommand(
            scene,
            EDITOR::MakeSceneObjectsHistoryCommand(
                std::move(history.label),
                std::move(history.beforeObjects),
                std::move(history.afterObjects),
                std::move(history.beforeCamera),
                std::move(history.afterCamera),
                impact),
            0,
            history.dirtyBefore &&
                !documentHistory_.IsDirty(),
            immediateImpact);
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
