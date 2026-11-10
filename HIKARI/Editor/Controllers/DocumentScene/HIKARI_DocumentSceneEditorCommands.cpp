#include "Editor/Project/HIKARI_GameProjectEditorWorkspaceHost.h"
#include "HIKARI/Project/EditorModuleUi.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include "Core/Text/HIKARI_Localization.h"
#include "Scene/HIKARI_GameObject.h"
#include <cstdint>
#include <span>
#include <utility>

#include "Core/HIKARI_Logger.h"
#include "Editor/Assets/HIKARI_FarModelBakeOperation.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/History/HIKARI_CinematicsHistoryCommand.h"
#include "Editor/History/HIKARI_SceneObjectsHistoryCommand.h"
#include "Editor/Play/HIKARI_EditorPlaySession.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/HIKARI_World.h"
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
    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Text)) {
        textWorkspaceController_.BindDocumentCommands(commandRouter_);
        return;
    }
    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::GameplayCamera)) {
        const auto* camera = scene.GetWorld().FindObject(GameplayCameraWorkspace().CameraId());
        // 固定機位は Scene の内容。保存・Undo を無関係な .hcamera 資産へ送らない。
        if (!camera || camera->GetComponent<CameraFollowComponent>()) {
            GameplayCameraWorkspace().BindDocumentCommands(commandRouter_);
            return;
        }
    }
    // 場面の履歴の元に戻す・やり直し（場景の工作区と、場面を直接編集する工作区で共通）。
    const auto bindSceneHistory = [this, &scene] {
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
    };
    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Project)) {
        if (projectWorkspaceHost_) {
            commandRouter_.Bind(EditorCommandId::SaveDocument, TEXT::Localize("menu.edit.save"),
                                projectWorkspaceHost_->Registry().HasActiveWorkspace(),
                                [this] { (void)projectWorkspaceHost_->Registry().RequestSaveActiveWorkspace(); });
        }
        if (projectWorkspaceHost_ == nullptr || projectWorkspaceHost_->ActiveWorkspaceRequestsSceneViewport()) {
            // 場景の視口を持つ項目の工作区（レベル編集、地形など）は場面を直接書き換えるので、
            // Ctrl+Z と編集メニューは場面の履歴へ送る。
            bindSceneHistory();
            return;
        }
        // 自分の文書を編集する項目の工作区（節点図など）には、元に戻す・やり直しをそのまま渡す。
        // 履歴は工作区の持ち物で、戻せるかどうかはここからは分からないので、工作区が開いていれば有効にする。
        const bool active = projectWorkspaceHost_->Registry().HasActiveWorkspace();
        commandRouter_.Bind(EditorCommandId::Undo, TEXT::Localize("menu.edit.undo"), active,
                            [this] { projectWorkspaceHost_->RequestHistoryStep(false); });
        commandRouter_.Bind(EditorCommandId::Redo, TEXT::Localize("menu.edit.redo"), active,
                            [this] { projectWorkspaceHost_->RequestHistoryStep(true); });
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
        bindSceneHistory();
    }

    if (!workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Scene) &&
        !workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::LookDev) &&
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
    // LookDev は自分の「落とす」（外接箱を下の面へ積む）を持つので、地面へ置くのは場面の工作区だけ。
    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Scene)) {
        commandRouter_.BindOperation(EditorCommandId::DropSelectionToGround, TEXT::Localize("ui.command.dropToGround"),
                                     context_.selection.GetSelectedObjectCount() != 0u && !scene.IsRuntimePlayActive(),
                                     "scene.drop_to_ground");
        commandRouter_.BindOperation(EditorCommandId::KeepPlayResults, TEXT::Localize("ui.command.keepPlayResults"),
                                     context_.selection.GetSelectedObjectCount() != 0u && scene.IsRuntimePlayActive(),
                                     "scene.keep_play_results");
    }
}

void DocumentSceneEditorController::ApplyKeptPlayResults(DocumentSceneBase& scene) {
    const bool playing = scene.IsRuntimePlayActive();
    const bool stopped = playWasActive_ && !playing;
    if (playing && !playWasActive_) {
        context_.keptPlayPoses.clear(); // 前の Play で残したまま止めなかった分は捨てる。
    }
    playWasActive_ = playing;
    if (!stopped || context_.keptPlayPoses.empty()) {
        return;
    }
    if (context_.keptPlayScenePath != scene.GetScenePath()) {
        HIKARI_LOG_WARN("Keep Play Results: Play ended in another scene; the kept poses were not written back.");
        context_.keptPlayPoses.clear();
        return;
    }
    // Play を止めると文書は Play 前へ戻っている。そこへ残した姿勢を書き、一つの撤销にする。
    SceneDocument& document = scene.GetSceneDocument();
    std::vector<SceneObjectData> before = document.objects;
    for (const KeptPlayPose& pose : std::exchange(context_.keptPlayPoses, {})) {
        if (SceneObjectData* object = FindSceneObjectData(document, pose.id);
            object != nullptr && !object->parent.has_value()) {
            object->transform = {pose.position, pose.rotationEulerDeg, pose.scale};
        }
    }
    if (before == document.objects) {
        return;
    }
    CommitSceneObjectAuthoringHistory(
        scene, SceneObjectAuthoringHistoryRequest{
                   "Keep Play Results", std::move(before), document.objects, document.camera, document.camera,
                   context_.sceneDirty || scene.HasUnsavedSceneChanges(), true, true, 0u});
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

namespace {

// モジュールの撤销の一件を場面の履歴へ載せる。場面の文書は変えない。
class ModuleUndoCommand final : public EDITOR::IEditorDocumentCommand {
public:
    ModuleUndoCommand(std::string label, std::unique_ptr<EDITOR_PROJECT::IEditorUndoEntry> entry)
        : label_(std::move(label)), entry_(std::move(entry)) {}
    const std::string& GetLabel() const noexcept override { return label_; }
    EDITOR::EditorDocumentImpact GetImpact() const noexcept override { return EDITOR::EditorDocumentImpact::None; }
    void Undo(SceneDocument&) const override { entry_->Undo(); }
    void Redo(SceneDocument&) const override { entry_->Redo(); }
    bool TryMergeApplied(const EDITOR::IEditorDocumentCommand&) override { return false; }

private:
    std::string label_;
    std::unique_ptr<EDITOR_PROJECT::IEditorUndoEntry> entry_;
};

} // namespace

void DocumentSceneEditorController::SetProjectWorkspaceHost(EDITOR::GameProjectEditorWorkspaceHost* host) {
    projectWorkspaceHost_ = host;
    if (host == nullptr) {
        return;
    }
    host->Extensions().SetUndoRecorder(
        [this](std::string label, std::unique_ptr<EDITOR_PROJECT::IEditorUndoEntry> entry) {
            documentHistory_.RecordApplied(std::make_unique<ModuleUndoCommand>(std::move(label), std::move(entry)));
            context_.sceneDirty = true;
            if (aiOperationScene_ != nullptr) {
                aiOperationScene_->SetUnsavedSceneChanges(true);
            }
        });
    host->Extensions().SetViewportCameraSetter(
        [this](const GameProjectFloat3& position, const GameProjectFloat3& target) {
            if (aiOperationScene_ != nullptr) {
                aiOperationScene_->GetCamera().SetLookAt({position.x, position.y, position.z},
                                                         {target.x, target.y, target.z});
            }
        });
    host->Extensions().SetSelectionReader([this]() {
        EDITOR_PROJECT::EditorSceneSelection selection{};
        selection.sceneObjectId = context_.selection.GetActiveObjectId().value;
        selection.selectedCount = static_cast<uint32_t>(context_.selection.GetSelectedObjectCount());
        const GameObject* object = context_.selection.selectedObject;
        const World* world = object != nullptr ? object->GetWorld() : nullptr;
        if (world != nullptr && object->GetRuntimeHandle().IsValid()) {
            selection.object = {GameProjectWorldHandle{world->GetRuntimeIdentity()},
                                object->GetRuntimeHandle().ToValue()};
        }
        return selection;
    });
    host->Extensions().SetPlayStateReader(
        [this]() { return aiPlaySession_ != nullptr && aiPlaySession_->IsRunning(); });
    host->Extensions().SetFarModelBaker([this](std::string_view modelAssetGuid) -> std::string {
        if (aiOperationScene_ == nullptr) {
            return {};
        }
        EDITOR::FarModelBakeOutcome outcome =
            EDITOR::BakeFarModelAsset(aiOperationScene_->GetAssetDatabase(), modelAssetGuid, 0u);
        if (!outcome.ok) {
            HIKARI_LOG_ERROR("[FarModel] " + outcome.message);
        }
        return std::move(outcome.farModelGuid);
    });
}

void DocumentSceneEditorController::DiscardModuleUndoEntries() { documentHistory_ = EDITOR::EditorDocumentHistory{}; }

bool DocumentSceneEditorController::SaveSceneDocument(DocumentSceneBase& scene) {
    if (scene.SaveCurrentSceneDocument()) {
        documentHistory_.MarkSaved();
        historyExternalDirty_ = false;
        context_.sceneDirty = false;
        scene.SetUnsavedSceneChanges(false);
        viewportDropMessage_ = "Scene saved";
        if (projectWorkspaceHost_ != nullptr) {
            projectWorkspaceHost_->Extensions().NotifySceneSaved(RuntimeSceneContext::GetCurrentSceneFilePath());
        }
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
    // 地面に従う物体を動かした・足した編集は、置き直しまで含めて一つの撤销にする。
    groundFollow_.ApplyToEdit(scene, history);
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
