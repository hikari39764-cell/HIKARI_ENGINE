#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Agent/HIKARI_AgentConnection.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Runtime/HIKARI_RuntimeLaunchConfig.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspacePresentation.h"
#include "Editor/Preview/HIKARI_EditorPreviewCapture.h"
#include "Editor/Commands/HIKARI_EditorOperationValue.h"
#include "Editor/Authoring/HIKARI_EditorWorkspaceSessionProviders.h"
#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"
#include "Editor/Project/HIKARI_GameProjectEditorWorkspaceHost.h"
#include "Core/HIKARI_TimeService.h"

#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_ParseNumber.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <vector>
#include "Assets/HIKARI_AssetImportState.h"
#include "Core/HIKARI_Logger.h"
#include "Editor/Authoring/HIKARI_EditorObjectState.h"
#include "Editor/Assets/Browser/HIKARI_AssetBrowserInternal.h"
#include "Editor/Menus/HIKARI_EditorDocumentMenu.h"
#include "Editor/Play/HIKARI_EditorPlaySession.h"
#include "Editor/Projects/HIKARI_EditorProjectSession.h"
#include "Editor/SystemAuthoring/HIKARI_BuiltInSystemAuthoring.h"
#include "Editor/Tools/HIKARI_BuiltInEditorTools.h"
#include "Editor/Views/HIKARI_EditorViewInstance.h"
#include "Editor/Workspaces/RuntimeBinding/HIKARI_RuntimeBindingReferenceIndex.h"
#include "Editor/HIKARI_EditorViewportInput.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Lighting/HIKARI_SceneLightingSet.h"

#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <json.hpp>

#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"

namespace HIKARI {

namespace {

void KeepWorkspaceDockSpacesAlive() {
    // 非表示の workspace も dock node を維持し、切替で window が浮くことを防ぐ。
    auto& nodes = ImGui::GetCurrentContext()->DockContext.Nodes;
    for (const auto& entry : nodes.Data) {
        const auto* node = static_cast<const ImGuiDockNode*>(entry.val_p);
        if (node && node->IsDockSpace() && !node->ParentNode)
            ImGui::DockSpace(node->ID, {}, ImGuiDockNodeFlags_KeepAliveOnly);
    }
}

} // namespace

DocumentSceneEditorController::DocumentSceneEditorController() {
    // 組み込みワークスペースも同じ起動引数で直接開ける。
    if (const auto requested = ReadRuntimeCommandLinePath(L"--open-workspace")) {
        const auto id = TEXT::PathToUtf8(*requested);
        for (const auto& workspace : EDITOR::kEditorWorkspacePresentations)
            if (EDITOR::EditorWorkspaceOperationId(workspace.id) == id) {
                EDITOR::EditorWorkspaceOpenRequest open;
                open.workspaceId = workspace.id;
                workspaceHost_.RequestOpen(open);
                break;
            }
    }
    // --agent-play-run <request.json>：後ろの無頭の编辑器として play.run を一回走らせる。
    pendingBackgroundRun_ = ReadRuntimeCommandLinePath(L"--agent-play-run");
    cameraPath_ = EDITOR::EditorCameraPath::FromCommandLine();
    qualitySchedule_ = EDITOR::EditorQualitySchedule::FromCommandLine();
    inputScript_ = EDITOR::EditorInputScript::FromCommandLine();
    // 診断用：--editor-debug-view <番号>（RenderDebugView の値、41 は間接拡散）で視口の表示を最初から切り替える。
    if (const auto debugView = ReadRuntimeCommandLineUint32(L"--editor-debug-view"))
        context_.viewportDebug.renderView = static_cast<RenderDebugView>(*debugView);
    EDITOR::RegisterBuiltInEditorTools(toolHost_);
    EDITOR::RegisterBuiltInSystemAuthoring(systemAuthoringRegistry_);
    agentHost_.SetPermissionResolver(
        [this](std::string_view operation) -> std::optional<EDITOR::AGENT::AgentOperationPolicy> {
            const EDITOR::EditorOperationDefinition* definition = operationRegistry_.Find(std::string(operation));
            if (definition == nullptr)
                return std::nullopt;
            return EDITOR::AGENT::AgentOperationPolicy{definition->permission, definition->approveEachTime,
                                                       definition->kind == EDITOR::EditorOperationKind::Query};
        });
    agentHost_.SetEditingCheck([this] { return IsUserEditingScene(); });
    // 無人の验收用。人が起動引数で入れる時だけ（面板の勾选・访问模式の切り替えと同じ扱い）。
    if (HasRuntimeCommandLineFlag(L"--agent-auto-approve"))
        agentHost_.SetAutoApprove(true);
    if (HasRuntimeCommandLineFlag(L"--agent-develop-project"))
        agentHost_.SetAccessMode(EDITOR::AGENT::AgentAccessMode::DevelopProject);
    agentHost_.SetExecutor([this](const std::string& operation, const nlohmann::json& parameters) {
        return ExecuteAgentAction(operation, parameters);
    });
    ModelMaterialWorkspace().SetOperationRegistry(&operationRegistry_);
    AnimationStateMachineWorkspace().SetOperationRegistry(&operationRegistry_);
    RuntimeBindingWorkspace().SetReferenceProvider([this](std::string_view bindingId) {
        return aiOperationScene_ != nullptr ? EDITOR::CollectRuntimeBindingReferences(*aiOperationScene_, bindingId)
                                            : std::vector<EDITOR::RuntimeBindingReference>{};
    });
    RuntimeBindingWorkspace().SetReferenceRevealCallback([this](const EDITOR::RuntimeBindingReference& reference) {
        if (reference.targetKind == "material.asset") {
            EDITOR::EditorWorkspaceOpenRequest request{};
            request.workspaceId = EDITOR::EditorWorkspaceId::ModelMaterial;
            request.materialAssetGuid = AssetGuid{reference.targetId};
            (void)workspaceHost_.RequestOpen(std::move(request));
            return;
        }
        if (reference.targetKind != "scene.component" || aiOperationScene_ == nullptr) {
            return;
        }
        uint64_t value = 0u;
        if (!TEXT::ParseNumber(reference.targetId, value) || value == 0u) {
            return;
        }
        SelectViewportObjects(*aiOperationScene_, {SceneObjectId{value}}, EditorObjectSelectionMode::Replace);
        EDITOR::EditorWorkspaceOpenRequest request{};
        request.workspaceId = EDITOR::EditorWorkspaceId::Scene;
        (void)workspaceHost_.RequestOpen(std::move(request));
    });
    VfxWorkspace().Preview().SetPreviewNamespaceRetireCallback(
        [](uint64_t playbackNamespace) { MESHRENDERER::ReleaseParticleNamespace(playbackNamespace); });
    VfxWorkspace().SetAssetRefreshCallback([this](const AssetGuid&, std::string& outMessage) {
        if (aiOperationScene_ == nullptr) {
            outMessage = "VFX source saved, but no active scene is available "
                         "for runtime asset refresh";
            return false;
        }
        if (!aiOperationScene_->ReloadAssets()) {
            outMessage = "VFX source saved, but runtime asset refresh failed";
            return false;
        }
        outMessage.clear();
        return true;
    });
    VfxWorkspace().SetMaterialEditCallback([this](const AssetGuid& materialGuid, std::string& outMessage) {
        if (!materialGuid.IsValid()) {
            outMessage = "Material GUID is empty";
            return false;
        }
        EDITOR::EditorWorkspaceOpenRequest request{};
        request.workspaceId = EDITOR::EditorWorkspaceId::ModelMaterial;
        request.materialAssetGuid = materialGuid;
        if (!workspaceHost_.RequestOpen(std::move(request))) {
            outMessage = "Failed to queue the Material workspace";
            return false;
        }
        outMessage = "Opening Material editor";
        return true;
    });
    RegisterEditorOperations();
    RegisterAgentOperations();
    RegisterProjectOperations();
    RegisterPlayOperations();
    EDITOR::RegisterVfxAuthoringSessions(authoringSessions_, workspaceHost_, [this] { return aiOperationScene_; });
    EDITOR::RegisterMaterialAuthoringSessions(authoringSessions_, workspaceHost_, [this] { return aiOperationScene_; });
    EDITOR::RegisterDocumentAuthoringSessions(authoringSessions_, workspaceHost_, [this] { return aiOperationScene_; });
    RegisterSceneAuthoringSessions();
    authoringSessions_.RegisterOperations(operationRegistry_);

    commandRouter_.SetOperationRegistry(&operationRegistry_);
}

EDITOR::EditorOperationResult DocumentSceneEditorController::ExecuteAgentAction(const std::string& operation,
                                                                                const nlohmann::json& parameters) {
    const EDITOR::EditorOperationDefinition* definition = operationRegistry_.Find(operation);
    if (definition == nullptr) {
        EDITOR::EditorOperationResult result =
            EDITOR::OperationFailure("UNKNOWN_OPERATION", "未注册的操作：" + operation);
        result.hints.push_back("Use operations.search with a keyword to find the operation id.");
        return result;
    }
    if (!EDITOR::HasEditorOperationExposure(definition->exposureMask, EDITOR::EditorOperationExposureMask::Agent))
        return EDITOR::OperationFailure("OPERATION_NOT_EXPOSED", "This operation is not exposed to agents.");
    if (!EDITOR::AGENT::AllowsAgentOperation(agentHost_.AccessMode(), operation))
        return EDITOR::OperationFailure("PROJECT_ACCESS_REQUIRED",
                                        "This project code operation is unavailable in the current access mode.");
    if (const std::optional<std::string> output =
            EDITOR::AGENT::FindEngineOutputParameter(*definition, parameters, engineRoots_)) {
        EDITOR::EditorOperationResult blocked = EDITOR::OperationFailure(
            "ENGINE_DIRECTORY_READ_ONLY", "Agents may not write into the engine directory: " + *output + ".");
        // 编辑器の作業目录が引擎の中にあると、相対パスは引擎目录に解決される。
        blocked.hints.push_back("Use an absolute path inside the project, or omit the path to keep the file in "
                                "Library; relative paths resolve against the editor's working directory.");
        return blocked;
    }
    // 一括操作の各手順も智能体の操作として確かめられるように、実行中だけ印を残す。
    struct Scope {
        bool& flag;
        bool previous;
        ~Scope() { flag = previous; }
    } scope{executingAgentAction_, std::exchange(executingAgentAction_, true)};
    // 编辑器から起動した智能体の依頼の間は、場面修改を一つの撤销组（撤销这一轮）にまとめる。
    const bool grouped = agentPanel_.turnActive;
    if (grouped)
        agentPanel_.turnUndoGroup = documentHistory_.BeginUndoGroup(agentPanel_.turnUndoGroup);
    const size_t undoDepth = documentHistory_.GetUndoDepth();
    EDITOR::EditorOperationResult result = operationRegistry_.Execute(operation, parameters);
    if (grouped)
        documentHistory_.EndUndoGroup();
    // この呼び出しが場面の撤销を積んだら、その名前を返す。次の scene.undo がこの修改を戻すかを智能体が判断できる。
    if (const std::string* label = documentHistory_.GetUndoLabel();
        label != nullptr && documentHistory_.GetUndoDepth() > undoDepth && result.data.is_object())
        result.data["undo"] = {{"label", *label}, {"top", true}};
    if (definition->kind == EDITOR::EditorOperationKind::Mutation)
        RecordAgentOperation(operation, result);
    return result;
}

bool DocumentSceneEditorController::IsUserEditingScene() const {
    // 文字の入力欄（智能体への依頼など）は数えない。マウスで動かしている間だけ。布景の寸法・开口の手柄は離した時に
    // 押し始めの設定から書き戻すので、その間に智能体が同じ形を変えると上書きされる。
    return viewportTransformHistory_.Active() || lookDevDrag_.has_value() || lookDevShapeDrag_.has_value() ||
           lookDevMainLightDrag_ || terrainStrokeActive_ ||
           (ImGui::IsAnyItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left));
}

EDITOR::EditorOperationResult DocumentSceneEditorController::ExecuteSceneObjectOperation(
    EDITOR::SceneObjectOperation operation, const nlohmann::json& parameters) {
    if (!aiOperationScene_)
        return {false, "NO_SCENE", "Scene context unavailable."};
    EDITOR::SceneObjectOperationContext context{*aiOperationScene_, context_, selectionSync_, sceneObjectCommands_};
    auto result = operation(context, parameters);
    if (context.history)
        CommitSceneObjectAuthoringHistory(*aiOperationScene_, std::move(*context.history));
    return result;
}

EDITOR::EditorOperationResult DocumentSceneEditorController::ExecuteTerrainOperation(
    EDITOR::TerrainAuthoringOperation operation, const nlohmann::json& parameters) {
    auto* scene = aiOperationScene_;
    if (!scene)
        return {false, "NO_SCENE", "Scene context unavailable."};
    const bool externalDirtyBefore = IsDirtyOutsideHistory(*scene);
    auto output = operation(*scene, parameters);
    if (output.command)
        CommitAppliedDocumentCommand(*scene, std::move(output.command), 0u, externalDirtyBefore,
                                     EDITOR::EditorDocumentImpact::None);
    if (output.reveal.value) {
        SelectViewportObjects(*scene, {output.reveal}, EditorObjectSelectionMode::Replace);
        EDITOR::EditorWorkspaceOpenRequest request;
        request.workspaceId = EDITOR::EditorWorkspaceId::TerrainSurface;
        if (!workspaceHost_.RequestOpen(std::move(request)))
            output.result.details.push_back("Terrain edited, but the workspace could not be opened.");
    }
    return std::move(output.result);
}

DocumentSceneEditorController::~DocumentSceneEditorController() { Shutdown(); }

void DocumentSceneEditorController::Shutdown() {
    VfxWorkspace().Deactivate(workspaceHost_.GetVfxPreviewView().renderViewId);
    agentPanel_.session.Stop();
    agentHost_.SetOpen(false);
    agentBridge_.Stop();
    CancelAiSceneCapture();
    authoringSessions_.Reset();
    aiOperationScene_ = nullptr;
}

EDITOR::EditorOperationResult DocumentSceneEditorController::ExecuteAiAssetImport(const nlohmann::json& parameters) {
    EDITOR::EditorOperationResult result{};
    DocumentSceneBase* scene = aiOperationScene_;
    if (scene == nullptr) {
        return EDITOR::OperationFailure("NO_SCENE", "场景上下文不可用");
    }
    const std::string sourcePathText = parameters.value("path", std::string{});
    if (sourcePathText.empty()) {
        return EDITOR::OperationFailure("MISSING_FIELD", "缺少 path 字段");
    }
    const std::filesystem::path sourcePath(sourcePathText);
    std::error_code fileError;
    if (!std::filesystem::is_regular_file(sourcePath, fileError)) {
        return EDITOR::OperationFailure("FILE_NOT_FOUND", "文件不存在或不可读：" + sourcePathText);
    }
    if (!EDITOR::ASSET_BROWSER::IsSupportedImportSource(sourcePath)) {
        return EDITOR::OperationFailure("UNSUPPORTED_TYPE", "不支持的文件类型：" + sourcePath.extension().string());
    }
    AssetDatabase& assetDatabase = scene->GetAssetDatabase();
    const std::filesystem::path targetRelative =
        EDITOR::ASSET_BROWSER::SuggestedTargetDirectory({}, sourcePath) / sourcePath.filename();
    std::filesystem::path copiedRelative;
    std::string copyError;
    if (!EDITOR::ASSET_BROWSER::CopySourceFileIntoProject(assetDatabase, sourcePath, targetRelative, false,
                                                          copiedRelative, copyError)) {
        return EDITOR::OperationFailure("COPY_FAILED", "导入复制失败：" + copyError);
    }
    if (!assetDatabase.ScanAssets(true)) {
        return EDITOR::OperationFailure("SCAN_FAILED", "资产扫描失败");
    }
    const AssetRecord* record = assetDatabase.FindByPath(copiedRelative);
    if (record == nullptr) {
        return EDITOR::OperationFailure("REGISTER_FAILED", "资产注册失败：" + copiedRelative.generic_string());
    }
    const bool queued = assetDatabase.QueueImportAssets({record->guid}, "AI import");
    result.ok = true;
    result.message =
        "已复制并注册 " + copiedRelative.generic_string() + (queued ? "，后台导入已排队" : "（已有导入批次运行中）");
    result.details = {
        "guid: " + record->guid.value,
        "可用 asset.import_status 查询进度",
    };
    return result;
}

EDITOR::EditorOperationResult DocumentSceneEditorController::ExecuteAiAssetImportStatus() {
    EDITOR::EditorOperationResult result{};
    DocumentSceneBase* scene = aiOperationScene_;
    if (scene == nullptr) {
        return EDITOR::OperationFailure("NO_SCENE", "场景上下文不可用");
    }
    const AssetImportBatchStatus status = scene->GetAssetDatabase().GetQueuedImportStatus();
    result.ok = true;
    result.message = status.active ? "导入进行中" : status.completed ? "导入已完成" : "没有活动导入";
    result.details = {
        "total: " + std::to_string(status.total),
        "finished: " + std::to_string(status.finished),
        "succeeded: " + std::to_string(status.succeeded),
        "failed: " + std::to_string(status.failed),
        "label: " + status.label,
    };
    if (!status.message.empty()) {
        result.details.push_back("message: " + status.message);
    }
    return result;
}

EDITOR::EditorOperationResult DocumentSceneEditorController::ExecuteAiUndoLast() {
    EDITOR::EditorOperationResult result{};
    DocumentSceneBase* scene = aiOperationScene_;
    if (scene == nullptr) {
        return EDITOR::OperationFailure("NO_SCENE", "场景上下文不可用");
    }
    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::ModelMaterial)) {
        return operationRegistry_.Execute("material.undo");
    }
    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::ModelCollision)) {
        return operationRegistry_.Execute("collision.undo");
    }
    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::AnimationStateMachine)) {
        return operationRegistry_.Execute("asm.undo");
    }
    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::RuntimeBinding)) {
        return operationRegistry_.Execute("binding.undo");
    }
    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Vfx)) {
        return operationRegistry_.Execute("vfx.undo");
    }
    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Cinematics) &&
        CinematicsWorkspace().IsEditingSequenceAsset()) {
        return operationRegistry_.Execute("cinematics.undo");
    }
    if (!documentHistory_.CanUndo()) {
        return EDITOR::OperationFailure("UNDO_EMPTY", "场景历史为空，没有可撤销的编辑");
    }
    ExecuteSceneDocumentHistory(*scene, false);
    return EDITOR::OperationOk("已撤销场景最近一次编辑");
}

EDITOR::EditorOperationResult DocumentSceneEditorController::ExecuteSceneSave() {
    EDITOR::EditorOperationResult result{};
    DocumentSceneBase* scene = aiOperationScene_;
    if (scene == nullptr) {
        return EDITOR::OperationFailure("NO_SCENE", "场景上下文不可用");
    }
    if (SaveSceneDocument(*scene)) {
        result.ok = true;
        result.code = "OK";
        result.message = "场景已保存";
    } else {
        result.code = "SAVE_FAILED";
        result.message = "保存失败；请先将场景保存为资源";
    }
    return result;
}

void DocumentSceneEditorController::EnsureAgentBridge(DocumentSceneBase& scene) {
    auto& integration = context_.aiIntegration;
    // 後ろで動かす無頭の引擎（play.background、hikari headless）は、人の编辑器の连接文件を上書きしないよう桥を開かない。
    static const bool bridgeOff = HasRuntimeCommandLineFlag(L"--agent-bridge-off");
    if (!integration.localBridgeEnabled || bridgeOff) {
        if (agentBridge_.IsRunning()) {
            // 待っている本地桥の要求を先に終わらせる。終わらない要求があると Stop が接続スレッドを待ち続ける。
            agentHost_.SetOpen(false);
            agentBridge_.Stop();
        }
        agentBridgeStartFailed_ = false;
        integration.localBridgeStatus.clear();
        return;
    }
    if (!agentBridge_.IsRunning()) {
        if (agentBridgeStartFailed_)
            return;
        std::string error;
        agentHost_.SetOpen(true);
        const bool started =
            agentBridge_.Start([this](std::string_view body) { return agentHost_.Execute(body); }, error);
        if (!started) {
            agentHost_.SetOpen(false);
            agentBridgeStartFailed_ = true;
            integration.localBridgeStatus = TEXT::Localize("settings.aiBridge.failed") + error;
            return;
        }
    }
    const auto path = EDITOR::AGENT::AgentConnectionPath(scene.GetAssetDatabase().GetLibraryRoot());
    if (agentBridge_.GetConnectionPath() == path)
        return;
    std::string error;
    if (!agentBridge_.PublishConnection(path, TEXT::PathToUtf8(scene.GetAssetDatabase().GetProjectRoot()), error)) {
        // 書けない場所なら止めて、利用者に理由を見せる。
        agentHost_.SetOpen(false);
        agentBridge_.Stop();
        agentBridgeStartFailed_ = true;
        integration.localBridgeStatus = TEXT::Localize("settings.aiBridge.failed") + error;
        return;
    }
    integration.localBridgeStatus = TEXT::Localize("settings.aiBridge.running") + agentBridge_.GetEndpoint() + "\n" +
                                    TEXT::Localize("settings.aiBridge.connectionFile") + TEXT::PathToUtf8(path);
}

void DocumentSceneEditorController::SnapshotSceneViewCameras(DocumentSceneBase& scene) {
    sceneViewEditorCameraSnapshot_ = scene.GetCamera();
    sceneViewDebugCameraSnapshot_ = scene.GetDebugCamera();
    sceneViewCameraSnapshotValid_ = true;
}

void DocumentSceneEditorController::RestoreSceneViewCameras(DocumentSceneBase& scene) {
    if (!sceneViewCameraSnapshotValid_) {
        return;
    }
    scene.GetCamera() = sceneViewEditorCameraSnapshot_;
    scene.GetDebugCamera() = sceneViewDebugCameraSnapshot_;
    sceneViewCameraSnapshotValid_ = false;
}

bool DocumentSceneEditorController::HasKnownUnsavedProjectChanges(const DocumentSceneBase& scene) const noexcept {
    return authoringSessions_.HasUnsavedDocuments() || uiWorkspaceController_.Animation().Document().IsDirty() ||
           (projectWorkspaceHost_ != nullptr && projectWorkspaceHost_->HasUnsavedChanges()) ||
           textWorkspaceController_.Document().IsDirty() || gameplayCameraWorkspaceController_.Document().IsDirty() ||
           context_.sceneDirty || scene.HasUnsavedSceneChanges() ||
           cinematicsWorkspaceController_.IsSequenceDocumentDirty() ||
           modelCollisionWorkspaceController_.IsDocumentDirty() ||
           modelMaterialWorkspaceController_.HasUnsavedMaterialChanges() ||
           animationStateMachineWorkspaceController_.IsDocumentDirty() ||
           runtimeBindingWorkspaceController_.IsDocumentDirty() || vfxWorkspaceController_.GetDocument().IsDirty() ||
           dialogueWorkspaceController_.IsDocumentDirty() || sceneAuthoringUtilityWindows_.HasUnsavedChanges() ||
           environmentPanel_.HasUnsavedDocuments();
}

void DocumentSceneEditorController::Draw(DocumentSceneBase& scene, EDITOR::EditorPlaySession& playSession,
                                         EDITOR::EditorProjectSessionController& projectSession) {
    aiOperationScene_ = &scene;
    aiPlaySession_ = &playSession;
    aiProjectSession_ = &projectSession;
    // 重载しても人が入れた访问模式と自動模式を保つ（智能体の作業が重载の後も続けられるように）。
    std::vector<std::wstring> carried;
    if (agentHost_.AccessMode() == EDITOR::AGENT::AgentAccessMode::DevelopProject)
        carried.emplace_back(L"--agent-develop-project");
    if (agentHost_.AutoApprove())
        carried.emplace_back(L"--agent-auto-approve");
    projectSession.SetCarriedEditorFlags(std::move(carried));
    KeepWorkspaceDockSpacesAlive();
    const auto projectRoot = scene.GetAssetDatabase().GetProjectRoot().generic_string();
    if (authoringProjectRoot_ != projectRoot) {
        CancelAiSceneCapture();
        authoringSessions_.Reset();
        authoringProjectRoot_ = projectRoot;
        authoringSessions_.SetRecoveryRoot(scene.GetAssetDatabase().GetLibraryRoot());
    }
    if (aiSceneCaptureTicket_ && (aiSceneCaptureIdentity_ != scene.GetSceneId() + "|" + scene.GetScenePath() ||
                                  aiSceneCaptureRevision_ != scene.GetSceneDocumentRevision()))
        CancelAiSceneCapture();
    if (cameraPath_ && !scene.IsRuntimePlayActive()) {
        // 编辑相机は操作器（DebugCameraController3D）の姿勢から毎フレーム作られるので、操作器も合わせる。
        cameraPath_->Apply(scene.GetCamera(), TIME::GetFrameContext().frameIndex);
        scene.GetDebugCamera().ResetFromCamera(scene.GetCamera());
    }
    if (qualitySchedule_)
        qualitySchedule_->Apply(TIME::GetFrameContext().frameIndex);
    if (inputScript_)
        inputScript_->Apply(TIME::GetFrameContext().frameIndex);
    agentHost_.PumpActions();
    TickAgentPlayRun();
    TickAgentPanel();
    authoringSessions_.Tick(static_cast<uint64_t>(ImGui::GetFrameCount()), ImGui::GetIO().DeltaTime);
    scene.GetAssetDatabase().PumpAssetTasks();
    SyncDocumentHistory(scene);
    context_.selection.RepairObjectSelection(scene.GetWorld());
    context_.gizmos.lockedObjectIds = EDITOR::CollectEditorLockedObjectIds(scene.GetSceneDocument());
    viewportSelectionService_.SetLockedObjectIds(context_.gizmos.lockedObjectIds);
    CinematicsWorkspace().SyncSceneIdentity(scene, workspaceHost_);
    ConfigureEditorCommands(scene);
    if (commandRouter_.ProcessDocumentShortcuts()) {
        ConfigureEditorCommands(scene);
    }
    // ~ でコンソールを開閉する。他の入力欄に打っている間は文字として通す（コンソールが開いていれば閉じる側に回す）。
    if ((toolHost_.IsOpen(EDITOR::kConsoleToolId) || !ImGui::GetIO().WantTextInput) &&
        EDITOR::ConsumeEditorShortcut(EDITOR::EditorShortcutScope::Document, ImGuiKey_GraveAccent)) {
        toolHost_.Toggle(EDITOR::kConsoleToolId);
    }
    // 試遊中にコンソールが開いている間は、ゲームの入力を止める（打鍵でゲームが動かないように）。
    // 編集中は止めない（編集カメラの操作を残す）。
    SERVICES::GetInputService().SetDeveloperInputCaptured(scene.IsRuntimePlayActive() &&
                                                          toolHost_.IsOpen(EDITOR::kConsoleToolId));

    documentToolbarController_.SyncDocumentMeta(scene, context_, selectionSync_);
    scene.SetUnsavedSceneChanges(context_.sceneDirty);
    // 操縦中の機位は視点そのものなので、選んでいても視錐台を描かない。
    if (context_.selection.selectedObject && !cameraPilot_.Active()) {
        scene.SetSelectedGizmoObjectId(context_.selection.selectedObject->GetDocumentId());
    } else {
        scene.SetSelectedGizmoObjectId(SceneObjectId{});
    }
    scene.SetComponentGizmoState(context_.gizmos);
    ViewportOverlayState overlays = context_.overlays;
    // 診断の相机経路で動かしている時は渲染の跳変だけを見たいので、灯や相机の辅助表示を描かない。
    overlays.cleanFrame = workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Cinematics) || cameraPath_.has_value();
    scene.SetViewportOverlayState(overlays);
    scene.SetViewportPerformanceState(context_.viewportPerformance);
    scene.SetViewportDebugViewState(context_.viewportDebug);

    bool resetDockingLayoutRequested = false;
    const auto previousWorkspace = workspaceHost_.GetActive();
    const bool performanceAuditWasOpen = context_.windows.runtime.showPerformanceAudit;
    exportJob_.Poll();
    projectCodeJob_.Poll();
    projectBuildAllowed_ = playSession.GetMode() == EDITOR::EditorPlayMode::None && !playSession.IsTransitioning();
    debugMenuBar_.Draw(context_.windows, toolHost_, workspaceHost_, scene.GetDebugCamera(),
                       scene.GetEnvironmentLightingEnabled(), resetDockingLayoutRequested, commandRouter_,
                       projectSession,
                       playSession.GetMode() == EDITOR::EditorPlayMode::None && !playSession.IsTransitioning(),
                       HasKnownUnsavedProjectChanges(scene), exportJob_, operationRegistry_);
    projectSession.DrawConfirmationDialog();

    if (projectWorkspaceHost_) {
        projectWorkspaceHost_->Registry().SetOperationRegistry(&operationRegistry_);
        projectWorkspaceHost_->UpdateNavigation(workspaceHost_, TIME::GetFrameContext().unscaledDt);
    }

    EnsureAgentBridge(scene);
    DrawGlobalShell(scene);

    if (resetDockingLayoutRequested) {
        workspaceHost_.RequestResetActiveLayout();
    }
    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Dialogue) &&
        authoringSessions_.VisibleSession("dialogue").empty() && DialogueWorkspace().IsDocumentDirty()) {
        const EDITOR::EditorWorkspaceOpenRequest* pendingRequest = workspaceHost_.PeekPending();
        if (pendingRequest != nullptr && pendingRequest->workspaceId != EDITOR::EditorWorkspaceId::Dialogue) {
            EDITOR::EditorWorkspaceOpenRequest guardedRequest = *pendingRequest;
            workspaceHost_.CancelPending();
            (void)DialogueWorkspace().RequestWorkspaceTransition(std::move(guardedRequest), viewportDropMessage_);
        }
    }
    if (std::optional<EDITOR::EditorWorkspaceActivation> activation = workspaceHost_.ApplyPending()) {
        // 表示中の文書を再ロードせず、バックグラウンドと同じ履歴に接続する。
        std::string activationAssetGuid;
        for (const auto* guid :
             {&activation->vfxAssetGuid, &activation->materialAssetGuid, &activation->animationStateMachineAssetGuid,
              &activation->dialogueAssetGuid, &activation->modelAssetGuid, &activation->sequenceAssetGuid,
              &activation->cameraProfileAssetGuid})
            if (*guid)
                activationAssetGuid = (*guid)->value;
        if (authoringSessions_.ActivateDocument(std::string(EDITOR::EditorWorkspaceOperationId(activation->current)),
                                                activationAssetGuid, HasForegroundDocument(activation->current))) {
            activation->vfxAssetGuid.reset();
            activation->materialAssetGuid.reset();
            activation->animationStateMachineAssetGuid.reset();
            activation->dialogueAssetGuid.reset();
            activation->modelAssetGuid.reset();
            activation->sequenceAssetGuid.reset();
            activation->cameraProfileAssetGuid.reset();
        }
        const auto isSceneOwnedWorkspace = [](EDITOR::EditorWorkspaceId id) {
            return id == EDITOR::EditorWorkspaceId::Scene || id == EDITOR::EditorWorkspaceId::LookDev ||
                   id == EDITOR::EditorWorkspaceId::Cinematics || id == EDITOR::EditorWorkspaceId::GameplayCamera ||
                   id == EDITOR::EditorWorkspaceId::Ui || id == EDITOR::EditorWorkspaceId::Text ||
                   id == EDITOR::EditorWorkspaceId::Count;
        };
        if (isSceneOwnedWorkspace(activation->previous) && !isSceneOwnedWorkspace(activation->current)) {
            SnapshotSceneViewCameras(scene);
        } else if (!isSceneOwnedWorkspace(activation->previous) && isSceneOwnedWorkspace(activation->current)) {
            RestoreSceneViewCameras(scene);
        }
        if (activation->previous == EDITOR::EditorWorkspaceId::Vfx &&
            activation->current != EDITOR::EditorWorkspaceId::Vfx) {
            VfxWorkspace().Deactivate(workspaceHost_.GetVfxPreviewView().renderViewId);
        }
        if (activation->previous != activation->current) {
            if (activation->previous == EDITOR::EditorWorkspaceId::Ui)
                uiWorkspaceController_.Deactivate(scene);
            if (activation->previous == EDITOR::EditorWorkspaceId::Text)
                textWorkspaceController_.Deactivate();
            playSession.ReleaseEmbeddedInput();
            EDITOR::ClearGameViewportInputRect();
        }
        GameplayCameraWorkspace().ApplyActivation(scene, *activation, workspaceHost_);
        textWorkspaceController_.ApplyActivation(scene, *activation);
        if (activation->current == EDITOR::EditorWorkspaceId::Ui && activation->uiAnimationAssetGuid)
            uiWorkspaceController_.Animation().Open(scene.GetAssetDatabase(), *activation->uiAnimationAssetGuid);
        CinematicsWorkspace().ApplyWorkspaceActivation(scene, *activation, context_, workspaceHost_);
        ModelCollisionWorkspace().ApplyWorkspaceActivation(scene, *activation, workspaceHost_);
        ModelMaterialWorkspace().ApplyWorkspaceActivation(scene, *activation, workspaceHost_);
        AnimationStateMachineWorkspace().ApplyWorkspaceActivation(scene, *activation);
        DialogueWorkspace().ApplyWorkspaceActivation(scene, *activation);
        if (activation->current == EDITOR::EditorWorkspaceId::RuntimeBinding) {
            std::string message{};
            if (!RuntimeBindingWorkspace().Activate(scene, message) || !message.empty()) {
                viewportDropMessage_ = std::move(message);
            }
        }
        if (activation->current == EDITOR::EditorWorkspaceId::Vfx) {
            std::string message{};
            const AssetGuid requestedGuid = activation->vfxAssetGuid.value_or(AssetGuid{});
            if (!VfxWorkspace().Activate(scene.GetAssetDatabase(), requestedGuid, message) || !message.empty()) {
                viewportDropMessage_ = std::move(message);
            }
        }
    }

    DrawWorkspaceContent(scene, playSession);
    // 場景以外の工作区（項目の工作区・地形など）の視口で動かした分も、離した時に一つの撤销として記録する。
    // 場景の工作区は ProcessSceneAuthoringRequests で先に記録済み。ここで拾わないと次のドラッグで上書きされて消える。
    if (auto history = viewportTransformHistory_.ConsumeHistoryRequest())
        CommitSceneObjectAuthoringHistory(scene, std::move(*history));
    // 診断窓は全工作区で共通。ドックを描いた後に復帰し、通常フレームではフォーカスを奪わない。
    if (context_.windows.runtime.showPerformanceAudit) {
        performanceAuditPanel_.Draw(context_.windows.runtime.showPerformanceAudit,
                                    !performanceAuditWasOpen || previousWorkspace != workspaceHost_.GetActive());
    }
    sceneInspectorPanel_.ExecutePendingObjectCommand(scene, context_, selectionSync_, sceneObjectCommands_);
    if (auto history = sceneObjectCommands_.ConsumeHistoryRequest())
        CommitSceneObjectAuthoringHistory(scene, std::move(*history));
    EDITOR::EditorToolContext toolContext{scene, context_, operationRegistry_};
    toolHost_.Draw(toolContext);
}

} // namespace HIKARI
