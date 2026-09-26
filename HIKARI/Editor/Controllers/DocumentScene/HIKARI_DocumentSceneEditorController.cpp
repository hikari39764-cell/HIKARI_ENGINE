#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Runtime/HIKARI_RuntimeLaunchConfig.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspacePresentation.h"
#include "Editor/Preview/HIKARI_EditorPreviewCapture.h"
#include "Editor/Commands/HIKARI_EditorOperationValue.h"
#include "Editor/Authoring/HIKARI_EditorWorkspaceSessionProviders.h"
#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"
#include "Editor/AI/HIKARI_AI_ProjectKnowledge.h"
#include "Editor/Project/HIKARI_GameProjectEditorWorkspaceHost.h"
#include "Core/HIKARI_TimeService.h"

#include "Core/Text/HIKARI_Localization.h"

#include <algorithm>
#include <limits>
#include <vector>
#include "Assets/HIKARI_AssetImportState.h"
#include "Core/HIKARI_Logger.h"
#include "Editor/AI/HIKARI_AI_ChatClient.h"
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
#include "Runtime/Presentation/HIKARI_FramePresentationService.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Runtime/Diagnostics/HIKARI_RuntimeDebugService.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Lighting/HIKARI_SceneLightingSet.h"

#include <cstdio>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <json.hpp>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"
#endif

namespace HIKARI {

namespace {
EDITOR::AI_ASSISTANT::AiRequestResult ToAiRequestResult(const EDITOR::EditorOperationResult& operation) {
    EDITOR::AI_ASSISTANT::AiRequestResult result{};
    result.ok = operation.ok;
    result.code = operation.code;
    result.message = operation.message;
    result.data = operation.data;
    result.details = operation.details;
    result.hints = operation.hints;
    return result;
}

#if defined(HIKARI_WITH_EDITOR)

void KeepWorkspaceDockSpacesAlive() {
    // 非表示の workspace も dock node を維持し、切替で window が浮くことを防ぐ。
    auto& nodes = ImGui::GetCurrentContext()->DockContext.Nodes;
    for (const auto& entry : nodes.Data) {
        const auto* node = static_cast<const ImGuiDockNode*>(entry.val_p);
        if (node && node->IsDockSpace() && !node->ParentNode)
            ImGui::DockSpace(node->ID, {}, ImGuiDockNodeFlags_KeepAliveOnly);
    }
}

#endif
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
    EDITOR::RegisterBuiltInEditorTools(toolHost_);
    EDITOR::RegisterBuiltInSystemAuthoring(systemAuthoringRegistry_);
    aiAssistantSession_.SetCapabilityProvider(
        [this]() {
            std::vector<EDITOR::AI_ASSISTANT::AiCapability> capabilities = EDITOR::AI_ASSISTANT::BuiltInCapabilities();
            for (const std::string& id : operationRegistry_.RegisteredIds()) {
                const EDITOR::EditorOperationDefinition* definition = operationRegistry_.Find(id);
                if (definition == nullptr ||
                    !EDITOR::HasEditorOperationExposure(definition->exposureMask,
                                                        EDITOR::EditorOperationExposureMask::BuiltInAi)) {
                    continue;
                }
                EDITOR::AI_ASSISTANT::AiCapability capability{definition->id, definition->summary, "stable"};
                capability.parameters.reserve(definition->parameters.size());
                capability.workspace = definition->workspace;
                capability.semantics = EDITOR::DescribeEditorOperation(*definition);
                capability.semantics.erase("parameters");
                capability.searchTerms = definition->searchTerms;
                for (const EDITOR::EditorOperationParameter& parameter : definition->parameters) {
                    capability.parameters.push_back(
                        {parameter.name, TEXT::EnumToName(EDITOR::kEditorOperationParameterTypeNames, parameter.type),
                         parameter.required, parameter.description, parameter.schema});
                }
                if (definition->requiresDocumentSession &&
                    authoringSessions_.SupportsWorkspace(definition->workspace) &&
                    std::none_of(capability.parameters.begin(), capability.parameters.end(),
                                 [](const auto& p) { return p.name == "sessionId"; })) {
                    capability.parameters.push_back({"sessionId", "string", true,
                                                     "Document session for workspace '" + definition->workspace +
                                                         "'; returned by authoring.session.open."});
                }
                capabilities.push_back(std::move(capability));
            }
            std::sort(capabilities.begin(), capabilities.end(),
                      [](const EDITOR::AI_ASSISTANT::AiCapability& lhs, const EDITOR::AI_ASSISTANT::AiCapability& rhs) {
                          return lhs.id < rhs.id;
                      });
            return capabilities;
        },
        [this] { return operationRegistry_.Revision(); });
    aiLocalBridge_.SetCapabilityProvider([this]() { return aiAssistantSession_.Capabilities(); });
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
        const char* begin = reference.targetId.data();
        const char* end = begin + reference.targetId.size();
        const auto parsed = std::from_chars(begin, end, value);
        if (parsed.ec != std::errc{} || parsed.ptr != end || value == 0u) {
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
    EDITOR::RegisterVfxAuthoringSessions(authoringSessions_, workspaceHost_, [this] { return aiOperationScene_; });
    EDITOR::RegisterMaterialAuthoringSessions(authoringSessions_, workspaceHost_, [this] { return aiOperationScene_; });
    EDITOR::RegisterDocumentAuthoringSessions(authoringSessions_, workspaceHost_, [this] { return aiOperationScene_; });
    RegisterSceneAuthoringSessions();
    authoringSessions_.RegisterOperations(operationRegistry_);
    aiAssistantSession_.RefreshCapabilities();
    aiAssistantSession_.SetSceneActionCallback([this](std::string_view json) {
        const std::string action = EDITOR::AI_ASSISTANT::ExtractJsonField(json, "action");
        if (operationRegistry_.IsRegistered(action)) {
            nlohmann::json parameters;
            try {
                parameters = nlohmann::json::parse(std::string(json));
            } catch (const nlohmann::json::parse_error& error) {
                EDITOR::AI_ASSISTANT::AiRequestResult result{};
                result.code = "JSON_PARSE_ERROR";
                result.message = "动作 JSON 解析失败";
                result.details.push_back(error.what());
                return result;
            }
            const bool background = parameters.value("_backgroundTask", false);
            parameters.erase("_backgroundTask");
            const auto* definition = operationRegistry_.Find(action);
            if (!EDITOR::HasEditorOperationExposure(definition->exposureMask,
                                                    EDITOR::EditorOperationExposureMask::BuiltInAi))
                return ToAiRequestResult(
                    {false, "OPERATION_NOT_EXPOSED", "This operation is not exposed to the assistant."});
#if defined(HIKARI_WITH_EDITOR)
            if (background && parameters.contains("sessionId") && parameters["sessionId"].is_string() &&
                definition->workspace == EDITOR::EditorWorkspaceOperationId(workspaceHost_.GetActive()) &&
                authoringSessions_.VisibleSession(definition->workspace) ==
                    parameters["sessionId"].get<std::string>() &&
                (ImGui::IsAnyItemActive() || ImGuizmo::IsUsing() ||
                 authoringSessions_.HasInteractiveEdit(parameters["sessionId"].get<std::string>()))) {
                const auto* activeWindow = ImGui::GetCurrentContext()->ActiveIdWindow;
                // 共通シェル・カメラ操作は文書の編集ロックではない。
                const auto name = activeWindow ? std::string_view(activeWindow->Name) : std::string_view{};
                const bool shell = name.find("Editor/") != std::string_view::npos ||
                                   name.find("AI Assistant") != std::string_view::npos;
                const bool view = name.find("View") != std::string_view::npos ||
                                  name.find("Preview") != std::string_view::npos ||
                                  name.find("Stage") != std::string_view::npos;
                const bool editing = ImGuizmo::IsUsing() ||
                                     authoringSessions_.HasInteractiveEdit(parameters["sessionId"].get<std::string>());
                if (editing || (!shell && (!view || ImGui::GetIO().WantTextInput ||
                                           ImGui::GetCurrentContext()->ActiveIdHasBeenEditedBefore)))
                    return ToAiRequestResult({false,
                                              "DOCUMENT_BUSY",
                                              "Document edit in progress; the assistant waits locally.",
                                              {{"retryable", true}}});
            }
#endif
            if (background && definition->requiresDocumentSession && !parameters.contains("sessionId") &&
                (authoringSessions_.SupportsWorkspace(definition->workspace) || action == "action.undo_last")) {
                return ToAiRequestResult(
                    {false,
                     "EXPLICIT_SESSION_REQUIRED",
                     "This operation needs an explicit document session.",
                     {{"workspace", definition->workspace},
                      {"operationSchema", EDITOR::DescribeEditorOperation(*definition)},
                      {"openSession", {{"action", "authoring.session.open"}, {"workspace", definition->workspace}}}}});
            }
            auto result = operationRegistry_.Execute(action, parameters);
            return ToAiRequestResult(result);
        }
        EDITOR::AI_ASSISTANT::AiRequestResult result{};
        result.ok = false;
        result.code = "UNKNOWN_ACTION";
        result.message = "不支持的 AI 动作：" + action;
        result.hints.push_back("Use query.capabilities with workspace or keyword to find the supported action.");
        return result;
    });

    aiAssistantSession_.SetContextProvider([this]() -> std::string {
        if (!aiOperationScene_)
            return {};
        auto& scene = *aiOperationScene_;
        std::string context = "scene objects: " + std::to_string(scene.GetWorld().GetObjects().size()) + " (";
        size_t shown = 0u;
        for (const auto& object : scene.GetWorld().GetObjects()) {
            if (shown >= 8u) {
                context += "...";
                break;
            }
            if (shown > 0u) {
                context += ", ";
            }
            context += "[" + std::to_string(object->GetDocumentId().value) + "] ";
            context += object->GetName();
            ++shown;
        }
        context +=
            ")\nmodel assets: " + std::to_string(scene.GetAssetRegistry().CollectByType(AssetType::Model).size());
        const auto active = workspaceHost_.GetActive();
        nlohmann::json document{{"workspace", EDITOR::EditorWorkspaceOperationId(active)}};
        if (nlohmann::json state = DescribeWorkspaceDocument(active, true); !state.is_null())
            document["document"] = std::move(state);
        if (active == EDITOR::EditorWorkspaceId::Scene)
            document["scenePath"] = scene.GetScenePath();
        context += "\nActive document: " + document.dump();
        context += "\nBackground authoring: " + authoringSessions_.Snapshot().dump();
        if (const std::string guide = EDITOR::ReadProjectGuideForContext(scene.GetAssetDatabase().GetProjectRoot());
            !guide.empty())
            context += "\nProject guide (user-authored conventions):\n" + guide;
        return context;
    });

    commandRouter_.SetOperationRegistry(&operationRegistry_);
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

DocumentSceneEditorController::~DocumentSceneEditorController() {
    VfxWorkspace().Deactivate(workspaceHost_.GetVfxPreviewView().renderViewId);
    aiAssistantSession_.Stop();
    aiLocalBridge_.Stop();
    CancelAiSceneCapture();
    authoringSessions_.Reset();
}

EDITOR::EditorOperationResult DocumentSceneEditorController::ExecuteAiCapturePreview(const nlohmann::json& parameters) {
    using namespace EDITOR;
    RENDER3D::RenderViewId view{};
    switch (workspaceHost_.GetActive()) {
    case EditorWorkspaceId::Scene:
    case EditorWorkspaceId::TerrainSurface: return CaptureAiScenePreview(parameters);
    case EditorWorkspaceId::Vfx: view = workspaceHost_.GetVfxPreviewView().renderViewId; break;
    case EditorWorkspaceId::ModelMaterial: view = workspaceHost_.GetModelMaterialPreviewView().renderViewId; break;
    case EditorWorkspaceId::ModelCollision: view = workspaceHost_.GetModelCollisionPreviewView().renderViewId; break;
    case EditorWorkspaceId::GameplayCamera: view = workspaceHost_.GetGameplayCameraPreviewView().renderViewId; break;
    case EditorWorkspaceId::Cinematics: view = workspaceHost_.GetCinematicsDirectorView().renderViewId; break;
    default: break;
    }
    const auto folder = aiOperationScene_
                            ? aiOperationScene_->GetAssetDatabase().GetLibraryRoot() / "EditorAI" / "Captures"
                            : std::filesystem::path{};
    return StartEditorPreviewCapture(view, folder, parameters.value("path", std::string{}));
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

void DocumentSceneEditorController::EnsureAiLocalBridge() {
    if (!context_.aiIntegration.localBridgeEnabled) {
        if (aiBridgeStarted_) {
            aiLocalBridge_.Stop();
            aiBridgeStarted_ = false;
        }
        aiAssistantSession_.SetBridgeSessionToken({});
        aiAssistantSession_.SetBridgeStatus("本地桥已禁用（可在编辑器设置中启用）");
        return;
    }
    if (aiBridgeStarted_) {
        aiAssistantSession_.SetBridgeStatus(aiLocalBridge_.IsRunning() ? "本地桥：" + aiLocalBridge_.GetEndpoint()
                                                                       : "本地桥已停止");
        return;
    }
    aiBridgeStarted_ = true;
    uint16_t port = 18725;
    if (const char* envPort = std::getenv("HIKARI_AI_BRIDGE_PORT")) {
        try {
            port = static_cast<uint16_t>(std::stoul(envPort));
        } catch (...) {
        }
    }
    std::string bridgeError;
    if (aiLocalBridge_.Start(
            port, [this](std::string_view json) { return aiAssistantSession_.ExecuteRequest(json); }, bridgeError)) {
        aiAssistantSession_.SetBridgeStatus("本地桥：" + aiLocalBridge_.GetEndpoint());
        aiAssistantSession_.SetBridgeSessionToken(aiLocalBridge_.GetSessionToken());
    } else {
        aiAssistantSession_.SetBridgeSessionToken({});
        aiAssistantSession_.SetBridgeStatus("本地桥启动失败：" + bridgeError);
    }
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
           textWorkspaceController_.Document().IsDirty() || gameplayCameraWorkspaceController_.Document().IsDirty() ||
           gameplayCameraWorkspaceController_.HasFlowDraft() || context_.sceneDirty || scene.HasUnsavedSceneChanges() ||
           cinematicsWorkspaceController_.IsSequenceDocumentDirty() ||
           modelCollisionWorkspaceController_.IsDocumentDirty() ||
           modelMaterialWorkspaceController_.HasUnsavedMaterialChanges() ||
           animationStateMachineWorkspaceController_.IsDocumentDirty() ||
           runtimeBindingWorkspaceController_.IsDocumentDirty() || vfxWorkspaceController_.GetDocument().IsDirty() ||
           dialogueWorkspaceController_.IsDocumentDirty() || sceneAuthoringUtilityWindows_.HasUnsavedChanges();
}

void DocumentSceneEditorController::Draw(DocumentSceneBase& scene, EDITOR::EditorPlaySession& playSession,
                                         EDITOR::EditorProjectSessionController& projectSession) {
#if defined(HIKARI_WITH_EDITOR)
    aiOperationScene_ = &scene;
    KeepWorkspaceDockSpacesAlive();
    const auto projectRoot = scene.GetAssetDatabase().GetProjectRoot().generic_string();
    if (authoringProjectRoot_ != projectRoot) {
        CancelAiSceneCapture();
        aiAssistantPanel_.ResetComposer();
        aiAssistantSession_.SwitchProject(scene.GetAssetDatabase().GetLibraryRoot());
        authoringSessions_.Reset();
        authoringProjectRoot_ = projectRoot;
        authoringSessions_.SetRecoveryRoot(scene.GetAssetDatabase().GetLibraryRoot());
    }
    if (aiAssistantSession_.ConsumeCancellation()) {
        authoringSessions_.CancelPreviews();
        CancelAiSceneCapture();
    }
    if (aiSceneCaptureTicket_ && (aiSceneCaptureIdentity_ != scene.GetSceneId() + "|" + scene.GetScenePath() ||
                                  aiSceneCaptureRevision_ != scene.GetSceneDocumentRevision()))
        CancelAiSceneCapture();
    aiAssistantSession_.PumpActions();
    aiAssistantPanel_.TickAttachments();
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

    documentToolbarController_.SyncDocumentMeta(scene, context_, selectionSync_);
    if (auto* diagnostics = scene.GetWorld().Services().Find<RUNTIME::RuntimeDebugService>())
        runtimeDebugWindow_.Draw(scene.GetWorld(), *diagnostics);
    else
        runtimeDebugWindow_.Reset();
    scene.SetUnsavedSceneChanges(context_.sceneDirty);
    if (context_.selection.selectedObject) {
        scene.SetSelectedGizmoObjectId(context_.selection.selectedObject->GetDocumentId());
    } else {
        scene.SetSelectedGizmoObjectId(SceneObjectId{});
    }
    scene.SetComponentGizmoState(context_.gizmos);
    scene.SetViewportOverlayState(context_.overlays);
    scene.SetViewportPerformanceState(context_.viewportPerformance);
    scene.SetViewportDebugViewState(context_.viewportDebug);

    bool resetDockingLayoutRequested = false;
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

    EnsureAiLocalBridge();
    static constexpr const char* workspaceLabels[] = {"menu.workspace.scene",
                                                      "menu.workspace.cinematics",
                                                      "menu.workspace.modelCollision",
                                                      "menu.workspace.modelMaterial",
                                                      "menu.workspace.animationStateMachine",
                                                      "menu.workspace.runtimeBinding",
                                                      "menu.workspace.vfx",
                                                      "menu.workspace.dialogue",
                                                      "menu.workspace.terrainSurface"};
    const auto workspaceIndex = static_cast<size_t>(workspaceHost_.GetActive());
    aiAssistantPanel_.SetWorkspaceLabel(workspaceIndex < std::size(workspaceLabels)
                                            ? TEXT::Localize(workspaceLabels[workspaceIndex])
                                            : scene.GetSceneId());
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
            return id == EDITOR::EditorWorkspaceId::Scene || id == EDITOR::EditorWorkspaceId::Cinematics ||
                   id == EDITOR::EditorWorkspaceId::GameplayCamera || id == EDITOR::EditorWorkspaceId::Ui ||
                   id == EDITOR::EditorWorkspaceId::Text || id == EDITOR::EditorWorkspaceId::Count;
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
#else
    (void)scene;
    (void)playSession;
    (void)projectSession;
#endif
}

} // namespace HIKARI
