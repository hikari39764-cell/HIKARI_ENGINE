#include "Editor/Project/HIKARI_GameProjectEditorWorkspaceHost.h"
#include "Editor/Shell/HIKARI_EditorDockLayout.h"
#include "Editor/Play/HIKARI_EditorPlaySession.h"
#include "Editor/History/HIKARI_SceneComponentPropertiesHistoryCommand.h"
#include "Editor/History/HIKARI_SceneSettingsHistoryCommand.h"
#include "Editor/History/HIKARI_SceneSystemsHistoryCommand.h"
#include "Editor/Widgets/HIKARI_EditorTexturePreview.h"
#include "Editor/Widgets/HIKARI_MaterialTextureSlotWidget.h"
#include "Editor/Workspaces/HIKARI_AssetWorkspaceActivationRouter.h"
#include "Editor/Workspaces/TerrainSurface/HIKARI_TerrainPlacementAuthoring.h"
#include "Editor/HIKARI_EditorViewportInput.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Runtime/Presentation/HIKARI_FramePresentationService.h"
#include "Editor/Tools/HIKARI_BuiltInEditorTools.h"
#include "RenderContracts/Material/HIKARI_MaterialAssetData.h"
#include "Core/HIKARI_TimeService.h"
#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include "Editor/Authoring/HIKARI_EditorObjectState.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Runtime/HIKARI_RuntimeResourceRefreshService.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#include <algorithm>

#include "imgui.h"
#include "imgui_internal.h"

namespace HIKARI {
namespace {
constexpr uint64_t kEnvironmentPanelMergeGroup = 0x454e5649524f4e4dull;
constexpr uint64_t kTerrainSurfaceWorkspaceMergeGroup = 0x5445525241494e53ull;

std::string SummarizeRuntimeRefreshReport(const RuntimeResourceRefreshReport& report) {
    return "Runtime refresh: texture " + std::to_string(report.textureInvalidatedCount) + ", sky " +
           std::to_string(report.skyInvalidatedCount) + ", model " + std::to_string(report.modelReloadedCount) +
           ", rebound " + std::to_string(report.modelReboundComponentCount) + ", material " +
           std::to_string(report.materialReloadedCount) + ", mat rebound " +
           std::to_string(report.materialReboundComponentCount) + ", failed " + std::to_string(report.failedCount);
}

bool CouldMutateEditorDocumentThisFrame() {
    const ImGuiIO& io = ImGui::GetIO();
    return ImGui::IsAnyItemActive() || io.WantTextInput || io.KeyCtrl || ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
           ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseReleased(ImGuiMouseButton_Left) ||
           ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false);
}

void DrawEditorDockSpace(bool resetDefaultDockLayout) {
    ImGuiIO& io = ImGui::GetIO();
    if ((io.ConfigFlags & ImGuiConfigFlags_DockingEnable) == 0) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    // 既定の配置を変えた時は ID の版を上げ、imgui.ini に残った古い配置を使わない。
    // v4: 環境（光と画づくり）を右の列の下半分へ移し、縦に長く使えるようにした。
    const ImGuiID dockspaceId = ImGui::GetID("HIKARI_EditorDockSpace_v4");
    const ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;

    static bool initializedDefaultDockLayout = false;
    if (!initializedDefaultDockLayout || resetDefaultDockLayout) {
        const bool needsDefaultLayout = ImGui::DockBuilderGetNode(dockspaceId) == nullptr;
        initializedDefaultDockLayout = true;
        if (!needsDefaultLayout && !resetDefaultDockLayout) {
            HIKARI::EDITOR::DrawWorkspaceDockSpace(dockspaceId, viewport, dockspaceFlags);
            return;
        }

        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace | dockspaceFlags);
        ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        ImGuiID mainNode = dockspaceId;
        ImGuiID leftNode = 0;
        ImGuiID rightNode = 0;
        ImGuiID environmentNode = 0;
        ImGuiID bottomNode = 0;
        ImGui::DockBuilderSplitNode(mainNode, ImGuiDir_Left, 0.19f, &leftNode, &mainNode);
        ImGui::DockBuilderSplitNode(mainNode, ImGuiDir_Right, 0.28f, &rightNode, &mainNode);
        ImGui::DockBuilderSplitNode(rightNode, ImGuiDir_Down, 0.5f, &environmentNode, &rightNode);
        ImGui::DockBuilderSplitNode(mainNode, ImGuiDir_Down, 0.24f, &bottomNode, &mainNode);

        ImGui::DockBuilderDockWindow("###Game View", mainNode);
        ImGui::DockBuilderDockWindow("###Scene Workspace", leftNode);
        ImGui::DockBuilderDockWindow("###Inspector", rightNode);
        ImGui::DockBuilderDockWindow("###Environment", environmentNode);
        ImGui::DockBuilderDockWindow("###Quality", bottomNode);
        ImGui::DockBuilderDockWindow("###Debug View", bottomNode);
        ImGui::DockBuilderDockWindow("###Performance Audit", bottomNode);

        ImGui::DockBuilderFinish(dockspaceId);
    }

    HIKARI::EDITOR::DrawWorkspaceDockSpace(dockspaceId, viewport, dockspaceFlags);
}
} // namespace

void DocumentSceneEditorController::DrawWorkspaceContent(DocumentSceneBase& scene,
                                                         EDITOR::EditorPlaySession& playSession) {
    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Project) && projectWorkspaceHost_) {
        projectWorkspaceHost_->Registry().SetOperationRegistry(&operationRegistry_);
        projectWorkspaceHost_->Draw(workspaceHost_, TIME::GetFrameContext().unscaledDt, scene.GetAssetDatabase(),
                                    scene.GetAssetRegistry());
        if (projectWorkspaceHost_->ActiveWorkspaceRequestsSceneViewport()) {
            projectWorkspaceHost_->PrepareSceneViewportWindow();
            DrawGameViewportWindow(scene, playSession, "Project Scene View");
        } else {
            playSession.ReleaseEmbeddedInput();
            EDITOR::ClearGameViewportInputRect();
            SERVICES::SetEditorGameViewportSize(0, 0, false);
        }
        return;
    }

    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::RuntimeBinding)) {
        SERVICES::SetEditorGameViewportSize(0, 0, false);
        RuntimeBindingWorkspace().DrawDockSpace(workspaceHost_.ConsumeReset(EDITOR::EditorWorkspaceId::RuntimeBinding));
        const EDITOR::RuntimeBindingWorkspaceResult result = RuntimeBindingWorkspace().Draw(scene, commandRouter_);
        FinishWorkspaceFrame(scene, result.exitToSceneRequested, result.statusMessage);
        return;
    }

    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Vfx)) {
        // The VFX workspace owns the presented realtime view. Do not keep
        // a hidden primary game/scene viewport rendering behind it.
        SERVICES::SetEditorGameViewportSize(0, 0, false);
        VfxWorkspace().DrawDockSpace(workspaceHost_.ConsumeReset(EDITOR::EditorWorkspaceId::Vfx));
        const EDITOR::VfxWorkspaceResult result = VfxWorkspace().Draw(
            scene.GetAssetDatabase(), scene.GetAssetRegistry(), workspaceHost_.GetVfxPreviewView(),
            static_cast<uint64_t>(ImGui::GetFrameCount()), ImGui::GetIO().DeltaTime, commandRouter_);
        FinishWorkspaceFrame(scene, result.exitToSceneRequested, result.statusMessage);
        return;
    }

    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Dialogue)) {
        SERVICES::SetEditorGameViewportSize(0, 0, false);
        DialogueWorkspace().DrawDockSpace(workspaceHost_.ConsumeReset(EDITOR::EditorWorkspaceId::Dialogue));
        const EDITOR::DialogueWorkspaceResult result = DialogueWorkspace().Draw(scene);
        if (result.workspaceRequest) {
            (void)workspaceHost_.RequestOpen(std::move(*result.workspaceRequest));
        }
        FinishWorkspaceFrame(scene, false, result.statusMessage);
        return;
    }

    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::AnimationStateMachine)) {
        SERVICES::SetEditorGameViewportSize(0, 0, false);
        AnimationStateMachineWorkspace().DrawDockSpace(
            workspaceHost_.ConsumeReset(EDITOR::EditorWorkspaceId::AnimationStateMachine));
        const EDITOR::AnimationStateMachineWorkspaceResult result =
            AnimationStateMachineWorkspace().Draw(scene, commandRouter_);
        FinishWorkspaceFrame(scene, result.exitToSceneRequested, result.statusMessage);
        SaveRenderQualityWhenIdle(scene);
        return;
    }

    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::ModelMaterial)) {
        SERVICES::SetEditorGameViewportSize(0, 0, false);
        ModelMaterialWorkspace().DrawDockSpace(workspaceHost_.ConsumeReset(EDITOR::EditorWorkspaceId::ModelMaterial));
        const EDITOR::ModelMaterialWorkspaceResult result =
            ModelMaterialWorkspace().Draw(scene, workspaceHost_, commandRouter_);
        FinishWorkspaceFrame(scene, result.exitToSceneRequested, result.statusMessage);
        SaveRenderQualityWhenIdle(scene);
        return;
    }

    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::ModelCollision)) {
        SERVICES::SetEditorGameViewportSize(0, 0, false);
        ModelCollisionWorkspace().DrawDockSpace(workspaceHost_.ConsumeReset(EDITOR::EditorWorkspaceId::ModelCollision));
        const EDITOR::ModelCollisionWorkspaceResult result =
            ModelCollisionWorkspace().Draw(scene, workspaceHost_, commandRouter_);
        FinishWorkspaceFrame(scene, result.exitToSceneRequested, result.statusMessage);
        SaveRenderQualityWhenIdle(scene);
        return;
    }

    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Text)) {
        textWorkspaceController_.DrawDockSpace(workspaceHost_.ConsumeReset(EDITOR::EditorWorkspaceId::Text));
        textWorkspaceController_.Draw(scene, operationRegistry_, commandRouter_);
        DrawPendingSceneOpenModal(scene);
        return;
    }
    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Ui)) {
        auto& workspace = uiWorkspaceController_;
        workspace.DrawDockSpace(workspaceHost_.ConsumeReset(workspaceHost_.GetActive()));
        auto result =
            workspace.Draw(scene, context_, selectionSync_, sceneObjectCommands_, commandRouter_, operationRegistry_);
        if (result.history)
            CommitSceneObjectAuthoringHistory(scene, std::move(*result.history));
        if (auto history = sceneObjectCommands_.ConsumeHistoryRequest())
            CommitSceneObjectAuthoringHistory(scene, std::move(*history));
        playSession.UpdateEmbeddedViewportInput(result.playViewport, result.playViewportClicked);
        if (result.togglePlay)
            ToggleGamePreview(scene, playSession);
        DrawPendingSceneOpenModal(scene);
        return;
    }
    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::GameplayCamera)) {
        GameplayCameraWorkspace().DrawDockSpace(workspaceHost_.ConsumeReset(EDITOR::EditorWorkspaceId::GameplayCamera));
        const auto result = GameplayCameraWorkspace().Draw(scene, workspaceHost_, operationRegistry_,
                                                           context_.selection.GetActiveObjectId());
        if (!ImGui::IsAnyItemActive())
            documentHistory_.SealMerge();
        playSession.UpdateEmbeddedViewportInput(result.playViewport, result.playViewportClicked);
        if (result.togglePlay)
            ToggleGamePreview(scene, playSession);
        DrawPendingSceneOpenModal(scene);
        return;
    }

    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Cinematics)) {
        DrawCinematicsWorkspace(scene, playSession);
        return;
    }
    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::TerrainSurface)) {
        DrawTerrainSurfaceWorkspace(scene, playSession);
        return;
    }
    if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::LookDev)) {
        DrawLookDevWorkspace(scene, playSession);
        return;
    }
    DrawSceneWorkspace(scene, playSession);
}

bool DocumentSceneEditorController::IsDirtyOutsideHistory(const DocumentSceneBase& scene) const {
    return historyExternalDirty_ ||
           ((context_.sceneDirty || scene.HasUnsavedSceneChanges()) && !documentHistory_.IsDirty());
}

void DocumentSceneEditorController::FinishWorkspaceFrame(DocumentSceneBase& scene, bool exitToScene,
                                                         const std::string& statusMessage) {
    if (exitToScene) {
        EDITOR::EditorWorkspaceOpenRequest request{};
        request.workspaceId = EDITOR::EditorWorkspaceId::Scene;
        (void)workspaceHost_.RequestOpen(std::move(request));
    }
    if (!statusMessage.empty()) {
        viewportDropMessage_ = statusMessage;
    }
    DrawPendingSceneOpenModal(scene);
}

void DocumentSceneEditorController::SaveRenderQualityWhenIdle(DocumentSceneBase& scene) {
    if (renderQualitySavePending_ && !ImGui::IsAnyItemActive()) {
        (void)SaveRenderQualityProfile(scene);
    }
}

void DocumentSceneEditorController::DrawOptionalGameViewport(DocumentSceneBase& scene,
                                                             EDITOR::EditorPlaySession& playSession,
                                                             std::string_view stableWindowId) {
    if (context_.windows.viewport.showGameView) {
        DrawGameViewportWindow(scene, playSession, stableWindowId);
        return;
    }
    playSession.ReleaseEmbeddedInput();
    viewportTransformHistory_.BeginFrame();
    viewportTransformHistory_.EndFrame(scene.GetSceneDocument());
    EDITOR::ClearGameViewportInputRect();
    SERVICES::SetEditorGameViewportSize(0, 0, false);
}

void DocumentSceneEditorController::DrawCinematicsWorkspace(DocumentSceneBase& scene,
                                                            EDITOR::EditorPlaySession& playSession) {
    SERVICES::SetEditorGameViewportSize(0, 0, false);
    const bool externalDirtyBefore = IsDirtyOutsideHistory(scene);
    std::optional<SceneCinematicsSettings> cinematicsBefore{};
    if (CouldMutateEditorDocumentThisFrame()) {
        cinematicsBefore = scene.GetSceneDocument().cinematics;
    }
    CinematicsWorkspace().DrawDockSpace(workspaceHost_.ConsumeReset(EDITOR::EditorWorkspaceId::Cinematics));
    const EDITOR::CinematicsWorkspaceResult result =
        CinematicsWorkspace().Draw(scene, context_, workspaceHost_, operationRegistry_);
    playSession.UpdateEmbeddedViewportInput(result.playViewport, result.playViewportClicked);
    if (result.cinematicsChanged && cinematicsBefore) {
        RecordCinematicsHistory(scene, std::move(*cinematicsBefore), result.timelineEditMergeId, externalDirtyBefore);
    } else if (result.timelineEditMergeId == 0 && !ImGui::IsAnyItemActive()) {
        documentHistory_.SealMerge();
    }
    if (result.saveSceneRequested) {
        (void)commandRouter_.Execute(EDITOR::EditorCommandId::SaveDocument);
    }
    FinishWorkspaceFrame(scene, false, result.statusMessage);
    SaveRenderQualityWhenIdle(scene);
}

void DocumentSceneEditorController::DrawTerrainSurfaceWorkspace(DocumentSceneBase& scene,
                                                                EDITOR::EditorPlaySession& playSession) {
    terrainSurfaceWorkspaceController_.DrawDockSpace(
        workspaceHost_.ConsumeReset(EDITOR::EditorWorkspaceId::TerrainSurface));
    DrawOptionalGameViewport(scene, playSession, "TerrainSurface/GameView");
    const bool externalDirtyBefore = IsDirtyOutsideHistory(scene);
    EDITOR::TerrainSurfaceWorkspaceResult terrainResult = terrainSurfaceWorkspaceController_.Draw(scene, context_);
    if (terrainResult.componentChanged) {
        std::unique_ptr<EDITOR::IEditorDocumentCommand> command =
            terrainResult.terrainDelta
                ? EDITOR::MakeSceneTerrainSurfaceEditDeltaHistoryCommand(
                      terrainResult.historyLabel.empty() ? "Edit Terrain Surface" : terrainResult.historyLabel,
                      terrainResult.objectId, terrainResult.componentId, std::move(terrainResult.beforePatch),
                      std::move(terrainResult.afterPatch), EDITOR::EditorDocumentImpact::RuntimeWorld)
                : EDITOR::MakeSceneComponentPropertiesPatchHistoryCommand(
                      "Edit Terrain Surface Settings", terrainResult.objectId, terrainResult.componentId,
                      std::move(terrainResult.beforePatch), std::move(terrainResult.afterPatch),
                      EDITOR::EditorDocumentImpact::RuntimeWorld);
        CommitAppliedDocumentCommand(scene, std::move(command),
                                     terrainResult.editActive ? kTerrainSurfaceWorkspaceMergeGroup : 0u,
                                     externalDirtyBefore, EDITOR::EditorDocumentImpact::None);
    }
    if (terrainResult.placeSelectionRequested) {
        std::vector<SceneObjectData> beforeObjects = scene.GetSceneDocument().objects;
        const SceneCameraSettings beforeCamera = scene.GetSceneDocument().camera;
        const EDITOR::TerrainPlacementResult placement = EDITOR::PlaceSelectedObjectsOnTerrain(
            scene, context_.selection, terrainResult.terrainTargetObjectId, terrainResult.placementOffset);
        if (!placement.changedObjectIds.empty()) {
            CommitSceneObjectAuthoringHistory(scene, SceneObjectAuthoringHistoryRequest{
                                                         "Place Objects on Terrain",
                                                         std::move(beforeObjects),
                                                         scene.GetSceneDocument().objects,
                                                         beforeCamera,
                                                         scene.GetSceneDocument().camera,
                                                         externalDirtyBefore,
                                                         true,
                                                         false,
                                                     });
            viewportDropMessage_ =
                "Placed " + std::to_string(placement.changedObjectIds.size()) + " object(s) on terrain.";
        } else {
            viewportDropMessage_ = "No selected object intersects the active terrain.";
        }
    }
    if (!terrainResult.editActive) {
        documentHistory_.SealMerge();
    }
    DrawPendingSceneOpenModal(scene);
}

void DocumentSceneEditorController::DrawSceneWorkspace(DocumentSceneBase& scene,
                                                       EDITOR::EditorPlaySession& playSession) {
    DrawEditorDockSpace(workspaceHost_.ConsumeReset(EDITOR::EditorWorkspaceId::Scene));

    DrawOptionalGameViewport(scene, playSession, "Game View");
    if (context_.windows.authoring.showSceneWorkspace) {
        DrawSceneWorkspaceWindow(scene);
    }
    if (context_.windows.authoring.showInspector) {
        DrawInspectorWindow(scene);
    }
    const SceneSystemsPanelResult systemsResult = sceneAuthoringUtilityWindows_.Draw(
        scene, context_.windows.authoring, SERVICES::GetInputService(), systemAuthoringRegistry_, toolHost_);
    if (systemsResult.changed) {
        const bool externalDirtyBefore = IsDirtyOutsideHistory(scene);
        CommitAppliedDocumentCommand(scene,
                                     EDITOR::MakeSceneSystemsHistoryCommand(systemsResult.label, systemsResult.before,
                                                                            scene.GetSceneDocument().systems),
                                     0, externalDirtyBefore, EDITOR::EditorDocumentImpact::Systems);
    }
    ProcessSceneAuthoringRequests(scene);
    HandleResourceWorkspaceRequests(scene);
    DrawEnvironmentPanels(scene);
    if (context_.windows.runtime.showDebugView) {
        DrawDebugViewWindow(scene, context_.windows.runtime.showDebugView);
    }
    DrawPendingSceneOpenModal(scene);
    SaveRenderQualityWhenIdle(scene);
}

void DocumentSceneEditorController::ProcessSceneAuthoringRequests(DocumentSceneBase& scene) {
    sceneCreationPanel_.DrawDeferredDialogs(scene, context_, selectionSync_);
    sceneInspectorPanel_.DrawDeferredDialogs(scene, context_, selectionSync_, sceneObjectCommands_);
    const auto recordSceneObjectHistory = [&](std::optional<SceneObjectAuthoringHistoryRequest> history) {
        if (!history) {
            return;
        }
        CommitSceneObjectAuthoringHistory(scene, std::move(*history));
    };
    recordSceneObjectHistory(sceneCreationPanel_.ConsumeHistoryRequest());
    recordSceneObjectHistory(sceneInspectorPanel_.ConsumeHistoryRequest());
    recordSceneObjectHistory(viewportTransformHistory_.ConsumeHistoryRequest());
    recordSceneObjectHistory(sceneObjectCommands_.ConsumeHistoryRequest());
    if (const std::optional<SceneObjectId> cameraRequest =
            sceneInspectorPanel_.ConsumeOpenCinematicsWorkspaceCameraRequest()) {
        EDITOR::EditorWorkspaceOpenRequest request{};
        request.workspaceId = EDITOR::EditorWorkspaceId::Cinematics;
        request.targetCameraObjectId = *cameraRequest;
        (void)workspaceHost_.RequestOpen(std::move(request));
    }
    if (const std::optional<SceneObjectId> focusRequest = sceneInspectorPanel_.ConsumeFocusObjectRequest()) {
        FocusSceneObjects(scene, {*focusRequest});
    }
}

void DocumentSceneEditorController::HandleResourceWorkspaceRequests(DocumentSceneBase& scene) {
    if (!context_.windows.resources.showAssetBrowser) {
        return;
    }
    if (const auto activation = resourceWorkspacePanel_.ConsumeActivatedAsset()) {
        EDITOR::AssetWorkspaceActivationRoute route = EDITOR::RouteAssetWorkspaceActivation(*activation);
        if (route.workspaceRequest.has_value()) {
            (void)workspaceHost_.RequestOpen(std::move(*route.workspaceRequest));
        } else if (route.sceneAssetGuid.has_value()) {
            RequestSceneOpen(scene, *route.sceneAssetGuid);
        }
    }

    const std::string saveSceneAsGuid = resourceWorkspacePanel_.ConsumeSaveSceneAsGuid();
    if (!saveSceneAsGuid.empty()) {
        if (scene.SaveCurrentSceneDocumentAs(AssetGuid{saveSceneAsGuid})) {
            context_.sceneDirty = false;
            scene.SetUnsavedSceneChanges(false);
            viewportDropMessage_ = "Scene saved to selected asset";
            HIKARI_LOG_INFO("[SceneAsset] save scene: " + saveSceneAsGuid);
        } else {
            viewportDropMessage_ = "Scene save target failed";
            HIKARI_LOG_WARN("[SceneAsset] save scene failed: " + saveSceneAsGuid);
        }
    }

    RuntimeResourceRefreshService refreshService{};
    auto applyRefreshReport = [this](RuntimeResourceRefreshReport report) {
        context_.lastRuntimeRefreshReport = std::move(report);
        viewportDropMessage_ = SummarizeRuntimeRefreshReport(context_.lastRuntimeRefreshReport);
    };

    const bool refreshCurrentScene = resourceWorkspacePanel_.ConsumeRefreshCurrentSceneResourcesRequested();
    const std::string refreshRuntimeGuid = resourceWorkspacePanel_.ConsumeRefreshRuntimeAssetGuid();
    const std::string reimportAndRefreshRuntimeGuid =
        resourceWorkspacePanel_.ConsumeReimportAndRefreshRuntimeAssetGuid();
    const std::string refreshRuntimeMaterialGuid = resourceWorkspacePanel_.ConsumeRefreshRuntimeMaterialGuid();
    std::vector<AssetGuid> targetedRefreshAssets{};
    const auto appendTargetedRefresh = [&targetedRefreshAssets](const std::string& guid) {
        if (guid.empty() ||
            std::ranges::any_of(targetedRefreshAssets, [&guid](const AssetGuid& id) { return id.value == guid; })) {
            return;
        }
        targetedRefreshAssets.push_back(AssetGuid{guid});
    };
    appendTargetedRefresh(refreshRuntimeGuid);
    appendTargetedRefresh(reimportAndRefreshRuntimeGuid);
    appendTargetedRefresh(refreshRuntimeMaterialGuid);

    bool assetRegistryPublished = false;
    if (refreshCurrentScene) {
        EDITOR::ClearEditorTexturePreviewCache();
        // 現在の SceneDocument が参照する実行時リソースをまとめて引き直す。
        RuntimeResourceRefreshReport report = refreshService.RefreshCurrentSceneResources(scene);
        assetRegistryPublished = report.assetRegistryPublished;
        applyRefreshReport(std::move(report));
    }

    if (!targetedRefreshAssets.empty() && (!refreshCurrentScene || assetRegistryPublished)) {
        EDITOR::ClearEditorTexturePreviewCache();
        applyRefreshReport(refreshService.RefreshAssets(scene, targetedRefreshAssets, assetRegistryPublished));
    }

    AssetGuid applyMaterialGuid{};
    PbrMaterialAssetData applyMaterialData{};
    if (resourceWorkspacePanel_.ConsumeApplyRuntimeMaterialRequest(applyMaterialGuid, applyMaterialData)) {
        const int rebuilt = scene.ApplyRuntimeMaterialOverridePreview(applyMaterialGuid, applyMaterialData);
        context_.lastRuntimeRefreshReport = {};
        context_.lastRuntimeRefreshReport.materialReboundComponentCount = rebuilt;
        context_.lastRuntimeRefreshReport.messages.push_back("Applied material runtime preview: " +
                                                             applyMaterialGuid.value);
        viewportDropMessage_ = SummarizeRuntimeRefreshReport(context_.lastRuntimeRefreshReport);
    }
}

void DocumentSceneEditorController::DrawEnvironmentPanels(DocumentSceneBase& scene) {
    bool environmentEditActive = false;
    if (context_.windows.resources.showEnvironment) {
        environmentEditActive = DrawEnvironmentWindow(scene, false);
    }
    if (context_.windows.resources.showQuality) {
        renderQualitySavePending_ |= qualityPanel_.Draw();
    }
    if (!environmentEditActive) {
        documentHistory_.SealMerge();
    }
}

bool DocumentSceneEditorController::DrawEnvironmentWindow(DocumentSceneBase& scene, bool lookDev) {
    const ENVIRONMENT::SceneEnvironmentBinding before = scene.GetSceneDocument().environment;
    ENVIRONMENT::SceneEnvironmentBinding edited = before;
    const EnvironmentPanelResult result =
        lookDev ? environmentPanel_.DrawLookDev(edited, scene) : environmentPanel_.Draw(edited, scene);
    if (result.bindingChanged && edited != before) {
        const bool externalDirtyBefore = IsDirtyOutsideHistory(scene);
        scene.GetSceneDocument().environment = edited;
        CommitAppliedDocumentCommand(
            scene, EDITOR::MakeEnvironmentHistoryCommand("Edit Environment", before, std::move(edited)),
            result.editActive ? kEnvironmentPanelMergeGroup : 0u, externalDirtyBefore,
            EDITOR::EditorDocumentImpact::Environment);
    }
    if (result.openLightingBakeRequested) {
        EDITOR::EditorToolOpenRequest request{};
        request.toolId = EDITOR::kLightingBakeToolId;
        (void)toolHost_.RequestOpen(std::move(request));
    }
    return result.editActive;
}

void DocumentSceneEditorController::DrawSceneWorkspaceWindow(DocumentSceneBase& scene) {
    if (!ImGui::Begin(TEXT::LocalizeWindow("ui.window.sceneWorkspace", "Scene Workspace").c_str())) {
        ImGui::End();
        return;
    }

    EDITOR::PanelTitle(TEXT::Localize("ui.sceneWorkspace.objects").c_str());
    ImGui::SameLine();
    const float createButtonWidth = ImGui::GetFrameHeight();
    ImGui::SetCursorPosX((std::max)(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - createButtonWidth));
    if (EDITOR::IconButton(EDITOR::EditorGlyph::Add, "CreateSceneObject", EDITOR::EditorButtonTone::Quiet,
                           ImVec2(createButtonWidth, createButtonWidth),
                           TEXT::Localize("ui.sceneWorkspace.createObject").c_str())) {
        ImGui::OpenPopup("SceneHierarchyCreateMenu");
    }
    if (ImGui::BeginPopup("SceneHierarchyCreateMenu")) {
        sceneCreationPanel_.DrawCreationMenu(scene, context_, selectionSync_, sceneObjectCommands_);
        ImGui::EndPopup();
    }

    hierarchyPanel_.DrawContents(
        scene.GetWorld(), context_.selection,
        [&](GameObject& object) {
            sceneInspectorPanel_.DrawObjectContextMenu(scene, context_, selectionSync_, sceneObjectCommands_, object);
        },
        [&](const GameObject& object) {
            return EDITOR::IsObjectEditorLocked(scene.GetSceneDocument(), object.GetDocumentId());
        },
        &scene.GetSceneDocument(), &operationRegistry_, scene.GetSceneDocumentRevision());

    if (ImGui::BeginPopupContextWindow("SceneHierarchyEmptyContext",
                                       ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        sceneCreationPanel_.DrawCreationMenu(scene, context_, selectionSync_, sceneObjectCommands_);
        ImGui::EndPopup();
    }

    if (!scene.IsRuntimePlayActive())
        (void)commandRouter_.ProcessViewportShortcuts(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));
    ImGui::End();
}

void DocumentSceneEditorController::DrawInspectorWindow(DocumentSceneBase& scene) {
    sceneInspectorPanel_.Draw(scene, context_, selectionSync_, sceneObjectCommands_,
                              &context_.windows.authoring.showInspector);
}

} // namespace HIKARI
