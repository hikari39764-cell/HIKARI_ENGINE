#include "HIKARI_DocumentSceneEditorController.h"
#include "Editor/Authoring/HIKARI_BoundSceneAuthoringSession.h"
#include "Editor/Preview/HIKARI_EditorPreviewCapture.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Runtime/Presentation/HIKARI_FramePresentationService.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_World.h"
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {
EDITOR::EditorOperationResult DocumentSceneEditorController::ExecuteAiCapturePreview(const nlohmann::json& parameters) {
    using namespace EDITOR;
    if (!aiOperationScene_)
        return OperationFailure("NO_SCENE", "No scene is open.");
    if (aiSceneCaptureTicket_ && !RENDER3D::EDITORVIEW::GetViewCaptureResult(aiSceneCaptureTicket_).done)
        return OperationFailure("CAPTURE_BUSY", "A viewport capture is still pending.");
    const std::string source = parameters.value("source", std::string("auto"));
    if (source != "auto" && source != "game" && source != "space" && source != "preview")
        return OperationFailure("INVALID_SOURCE", "source accepts auto, game, space or preview.");
    RENDER3D::RenderViewId view{};
    const auto workspace = workspaceHost_.GetActive();
    if (source == "game") {
        view = RENDER3D::kPrimaryRenderViewId;
    } else if (source == "space") {
        if (workspace == EditorWorkspaceId::Cinematics)
            view = workspaceHost_.GetCinematicsDirectorView().renderViewId;
        else if (workspace == EditorWorkspaceId::GameplayCamera)
            view = workspaceHost_.GetGameplayCameraObserverView().renderViewId;
    } else {
        switch (workspace) {
        case EditorWorkspaceId::Scene:
        case EditorWorkspaceId::TerrainSurface:
        case EditorWorkspaceId::Ui:
        case EditorWorkspaceId::Cinematics: view = RENDER3D::kPrimaryRenderViewId; break;
        case EditorWorkspaceId::GameplayCamera:
            view = aiOperationScene_->IsRuntimePlayActive()
                       ? RENDER3D::kPrimaryRenderViewId
                       : workspaceHost_.GetGameplayCameraPreviewView().renderViewId;
            break;
        case EditorWorkspaceId::Vfx: view = workspaceHost_.GetVfxPreviewView().renderViewId; break;
        case EditorWorkspaceId::ModelMaterial: view = workspaceHost_.GetModelMaterialPreviewView().renderViewId; break;
        case EditorWorkspaceId::ModelCollision:
            view = workspaceHost_.GetModelCollisionPreviewView().renderViewId;
            break;
        default: break;
        }
        if (source == "preview" && view == RENDER3D::kPrimaryRenderViewId)
            view = {};
    }
    if (view == RENDER3D::kPrimaryRenderViewId && !SERVICES::IsEditorGameViewportVisible())
        return OperationFailure("VIEW_NOT_VISIBLE",
                                "Show the Game View / output monitor before capturing game output.");
    CancelAiSceneCapture();
    auto& scene = *aiOperationScene_;
    aiSceneCaptureIdentity_ = scene.GetSceneId() + "|" + scene.GetScenePath();
    aiSceneCaptureRevision_ = scene.GetSceneDocumentRevision();
    auto result = StartEditorPreviewCapture(view, scene.GetAssetDatabase().GetLibraryRoot() / "EditorAI" / "Captures",
                                            parameters.value("path", std::string{}));
    if (result.ok && result.data.contains("poll"))
        aiSceneCaptureTicket_ = result.data["poll"].value("captureId", uint64_t{});
    return result;
}

void DocumentSceneEditorController::CancelAiSceneCapture() {
    sceneViewSeries_.Clear();
    if (aiSceneCaptureTicket_)
        RENDER3D::EDITORVIEW::CancelViewCapture(aiSceneCaptureTicket_);
    aiSceneCaptureTicket_ = 0;
}
EDITOR::EditorOperationResult DocumentSceneEditorController::PollAiPreviewCapture(uint64_t id) {
    auto result = EDITOR::PollEditorPreviewCapture(id);
    if (id == aiSceneCaptureTicket_ && (!result.ok || !result.data.value("pending", false))) {
        aiSceneCaptureTicket_ = 0;
    }
    return result;
}
void DocumentSceneEditorController::RegisterSceneAuthoringSessions() {
    EDITOR::RegisterBoundSceneAuthoringSessions(authoringSessions_, operationRegistry_, workspaceHost_,
                                                {[this] { return aiOperationScene_; },
                                                 [this] { return documentHistory_.GetEditRevision(); },
                                                 [this](const auto& definition, const auto& parameters) {
                                                     return ExecuteBackgroundSceneOperation(definition, parameters);
                                                 }});
}
EDITOR::EditorOperationResult DocumentSceneEditorController::ExecuteBackgroundSceneOperation(
    const EDITOR::EditorOperationDefinition& definition, const nlohmann::json& parameters) {
    if (!aiOperationScene_)
        return {false, "NO_SCENE", "No scene is open."};
    if (definition.kind == EDITOR::EditorOperationKind::Query)
        return definition.execute(parameters);
    if (aiOperationScene_->IsRuntimePlayActive())
        return {false, "PLAY_ACTIVE", "Stop Play before editing the scene."};
#if defined(HIKARI_WITH_EDITOR)
    if (ImGui::IsAnyItemActive() && (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Scene) ||
                                     workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::TerrainSurface)))
        return {false, "DOCUMENT_BUSY", "用户正在编辑场景。请等待当前操作完成后重试。"};
#endif
    auto& scene = *aiOperationScene_;
    const auto selection = context_.selection;
    const auto windows = context_.windows;
    const auto terrain = context_.terrainTool;
    const auto camera = scene.GetCamera();
    const std::optional<EDITOR::EditorWorkspaceOpenRequest> pending =
        workspaceHost_.PeekPending() ? std::optional{*workspaceHost_.PeekPending()} : std::nullopt;
    const auto restore = [&] {
        context_.selection = selection;
        context_.selection.RepairObjectSelection(scene.GetWorld(), selection.GetActiveObjectId());
        context_.windows = windows;
        context_.terrainTool = terrain;
        scene.GetCamera() = camera;
        workspaceHost_.CancelPending();
        if (pending)
            workspaceHost_.RequestOpen(*pending);
    };
    try {
        auto result = definition.execute(parameters);
        restore();
        return result;
    } catch (...) {
        restore();
        throw;
    }
}
} // namespace HIKARI
