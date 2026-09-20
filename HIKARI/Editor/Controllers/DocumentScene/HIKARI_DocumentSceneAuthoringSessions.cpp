#include "HIKARI_DocumentSceneEditorController.h"
#include "Editor/Authoring/HIKARI_BoundSceneAuthoringSession.h"
#include "Editor/Authoring/HIKARI_EditorAuthoringPreview.h"
#include "Editor/Preview/HIKARI_EditorPreviewCapture.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include <algorithm>
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_World.h"
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {
void DocumentSceneEditorController::CancelAiSceneCapture() {
    if (aiSceneCaptureTicket_) RENDER3D::EDITORVIEW::CancelViewCapture(aiSceneCaptureTicket_);
    if (aiSceneCaptureView_.value) RENDER3D::EDITORVIEW::ClearRequest(aiSceneCaptureView_);
    aiSceneCaptureTicket_=0;
}
EDITOR::EditorOperationResult DocumentSceneEditorController::CaptureAiScenePreview(const nlohmann::json& parameters) {
    if (!aiOperationScene_) return {false,"NO_SCENE","No scene is open."};
    if (aiSceneCaptureTicket_ && !RENDER3D::EDITORVIEW::GetViewCaptureResult(aiSceneCaptureTicket_).done)
        return {false,"CAPTURE_BUSY","A scene preview is already pending."};
    CancelAiSceneCapture();
    if (!aiSceneCaptureView_.value) aiSceneCaptureView_={EDITOR::AllocateAuthoringPreviewViewId()};
    auto& scene=*aiOperationScene_;
    RENDER3D::EDITORVIEW::EditorInteractiveViewRequest request{};
    request.viewId=aiSceneCaptureView_;request.cameraFrame=scene.GetResolvedCameraFrame();
    request.width=768;
    request.height=std::clamp(static_cast<uint32_t>(768.0f/std::clamp(request.cameraFrame.camera.GetAspect(),0.5f,3.0f)),128u,1536u);
    request.cameraFrame.camera.SetAspect(static_cast<float>(request.width)/request.height);
    request.visible=true;request.drawDebug=false;request.sceneRevision=scene.GetSceneDocumentRevision();
    aiSceneCaptureIdentity_=scene.GetSceneId()+"|"+scene.GetScenePath();
    aiSceneCaptureRevision_=request.sceneRevision;
    RENDER3D::EDITORVIEW::SubmitRequest(request);
    auto result=EDITOR::StartEditorPreviewCapture(aiSceneCaptureView_,scene.GetAssetDatabase().GetLibraryRoot()/"EditorAI"/"Captures",parameters.value("path",std::string{}));
    if (result.ok && result.data.contains("poll")) aiSceneCaptureTicket_=result.data["poll"].value("captureId",uint64_t{});
    else RENDER3D::EDITORVIEW::ClearRequest(aiSceneCaptureView_);
    return result;
}
EDITOR::EditorOperationResult DocumentSceneEditorController::PollAiPreviewCapture(uint64_t id) {
    auto result=EDITOR::PollEditorPreviewCapture(id);
    if (id==aiSceneCaptureTicket_ && (!result.ok || !result.data.value("pending",false))) {
        RENDER3D::EDITORVIEW::ClearRequest(aiSceneCaptureView_); aiSceneCaptureTicket_=0;
    }
    return result;
}
void DocumentSceneEditorController::RegisterSceneAuthoringSessions() {
    EDITOR::RegisterBoundSceneAuthoringSessions(authoringSessions_,operationRegistry_,workspaceHost_,{
        [this] { return aiOperationScene_; },
        [this] { return documentHistory_.GetEditRevision(); },
        [this](const auto& definition,const auto& parameters) { return ExecuteBackgroundSceneOperation(definition,parameters); }});
}
EDITOR::EditorOperationResult DocumentSceneEditorController::ExecuteBackgroundSceneOperation(
    const EDITOR::EditorOperationDefinition& definition,const nlohmann::json& parameters) {
    if (!aiOperationScene_) return {false,"NO_SCENE","No scene is open."};
    if (aiOperationScene_->IsRuntimePlayActive()) return {false,"PLAY_ACTIVE","Stop Play before editing the scene."};
#if defined(HIKARI_WITH_EDITOR)
    if (ImGui::IsAnyItemActive() && (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Scene) ||
            workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::TerrainSurface)))
        return {false,"DOCUMENT_BUSY","用户正在编辑场景。请等待当前操作完成后重试。"};
#endif
    auto& scene=*aiOperationScene_;
    const auto selection=context_.selection;
    const auto windows=context_.windows;
    const auto terrain=context_.terrainTool;
    const auto camera=scene.GetCamera();
    const std::optional<EDITOR::EditorWorkspaceOpenRequest> pending=workspaceHost_.PeekPending() ?
        std::optional{*workspaceHost_.PeekPending()} : std::nullopt;
    const auto restore=[&] {
        context_.selection=selection;
        context_.selection.RepairObjectSelection(scene.GetWorld(),selection.GetActiveObjectId());
        context_.windows=windows;
        context_.terrainTool=terrain;
        scene.GetCamera()=camera;
        workspaceHost_.CancelPending();
        if (pending) workspaceHost_.RequestOpen(*pending);
    };
    try {
        auto result=definition.execute(parameters);
        restore();return result;
    } catch (...) { restore();throw; }
}
}
