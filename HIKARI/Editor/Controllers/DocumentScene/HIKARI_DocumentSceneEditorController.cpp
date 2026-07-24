#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Core/HIKARI_Logger.h"
#include "Editor/Authoring/HIKARI_EditorObjectState.h"
#include "Editor/History/HIKARI_SceneObjectsHistoryCommand.h"
#include "Editor/History/HIKARI_SceneSystemsHistoryCommand.h"
#include "Editor/Menus/HIKARI_EditorDocumentMenu.h"
#include "Editor/Play/HIKARI_EditorPlaySession.h"
#include "Editor/SystemAuthoring/HIKARI_BuiltInSystemAuthoring.h"
#include "Editor/Tools/HIKARI_BuiltInEditorTools.h"
#include "Editor/Widgets/HIKARI_MaterialTextureSlotWidget.h"
#include "HIKARI_Services.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Runtime/HIKARI_RuntimeResourceRefreshService.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#include <utility>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

namespace HIKARI {

    namespace {
        std::string SummarizeRuntimeRefreshReport(const RuntimeResourceRefreshReport& report) {
            return "Runtime refresh: texture " + std::to_string(report.textureInvalidatedCount) +
                ", sky " + std::to_string(report.skyInvalidatedCount) +
                ", model " + std::to_string(report.modelReloadedCount) +
                ", rebound " + std::to_string(report.modelReboundComponentCount) +
                ", material " + std::to_string(report.materialReloadedCount) +
                ", mat rebound " + std::to_string(report.materialReboundComponentCount) +
                ", failed " + std::to_string(report.failedCount);
        }

#if defined(HIKARI_WITH_EDITOR)
        bool CouldMutateEditorDocumentThisFrame() {
            const ImGuiIO& io = ImGui::GetIO();
            return ImGui::IsAnyItemActive() || io.WantTextInput ||
                io.KeyCtrl ||
                ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                ImGui::IsMouseReleased(ImGuiMouseButton_Left) ||
                ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
                ImGui::IsKeyPressed(ImGuiKey_Backspace, false);
        }

        void DrawEditorDockSpace(bool resetDefaultDockLayout) {
            ImGuiIO& io = ImGui::GetIO();
            if ((io.ConfigFlags & ImGuiConfigFlags_DockingEnable) == 0) {
                return;
            }

            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const ImGuiID dockspaceId = ImGui::GetID("HIKARI_EditorDockSpace");
            const ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;

            static bool initializedDefaultDockLayout = false;
            if (!initializedDefaultDockLayout || resetDefaultDockLayout) {
                const bool needsDefaultLayout = ImGui::DockBuilderGetNode(dockspaceId) == nullptr;
                initializedDefaultDockLayout = true;
                if (!needsDefaultLayout && !resetDefaultDockLayout) {
                    ImGui::DockSpaceOverViewport(dockspaceId, viewport, dockspaceFlags);
                    return;
                }

                ImGui::DockBuilderRemoveNode(dockspaceId);
                ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace | dockspaceFlags);
                ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);
                ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

                ImGuiID mainNode = dockspaceId;
                ImGuiID leftNode = 0;
                ImGuiID rightNode = 0;
                ImGuiID bottomNode = 0;
                ImGui::DockBuilderSplitNode(
                    mainNode,
                    ImGuiDir_Left,
                    0.19f,
                    &leftNode,
                    &mainNode);
                ImGui::DockBuilderSplitNode(
                    mainNode,
                    ImGuiDir_Down,
                    0.24f,
                    &bottomNode,
                    &mainNode);
                ImGui::DockBuilderSplitNode(
                    mainNode,
                    ImGuiDir_Right,
                    0.28f,
                    &rightNode,
                    &mainNode);

                ImGui::DockBuilderDockWindow("Game View", mainNode);
                ImGui::DockBuilderDockWindow("Scene Workspace", leftNode);
                ImGui::DockBuilderDockWindow("Inspector", rightNode);
                ImGui::DockBuilderDockWindow("Resource Workspace", bottomNode);
                ImGui::DockBuilderDockWindow("Environment", bottomNode);
                ImGui::DockBuilderDockWindow("Quality", bottomNode);
                ImGui::DockBuilderDockWindow("Lighting Bake", bottomNode);
                ImGui::DockBuilderDockWindow("Diagnostics", bottomNode);
                ImGui::DockBuilderDockWindow("Debug View", bottomNode);
                ImGui::DockBuilderDockWindow("Performance Audit", bottomNode);
                ImGui::DockBuilderDockWindow("Renderer Health", bottomNode);

                ImGui::DockBuilderFinish(dockspaceId);
            }

            ImGui::DockSpaceOverViewport(dockspaceId, viewport, dockspaceFlags);
        }
#endif
    }

    DocumentSceneEditorController::DocumentSceneEditorController() {
        EDITOR::RegisterBuiltInEditorTools(toolHost_);
        EDITOR::RegisterBuiltInSystemAuthoring(
            systemAuthoringRegistry_);
    }

    void DocumentSceneEditorController::Draw(
        DocumentSceneBase& scene,
        EDITOR::EditorPlaySession& playSession) {
#if defined(HIKARI_WITH_EDITOR)
        scene.GetAssetDatabase().PumpAssetTasks();
        SyncDocumentHistory(scene);
        context_.selection.RepairObjectSelection(scene.GetWorld());
        context_.gizmos.lockedObjectIds =
            EDITOR::CollectEditorLockedObjectIds(
                scene.GetSceneDocument());
        viewportSelectionService_.SetLockedObjectIds(
            context_.gizmos.lockedObjectIds);
        cinematicsWorkspaceController_.SyncSceneIdentity(
            scene,
            workspaceHost_);
        ConfigureEditorCommands(scene);
        if (commandRouter_.ProcessDocumentShortcuts()) {
            ConfigureEditorCommands(scene);
        }

        documentToolbarController_.SyncDocumentMeta(scene, context_, selectionSync_);
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
        debugMenuBar_.Draw(
            context_.windows,
            toolHost_,
            workspaceHost_,
            scene.GetDebugCamera(),
            scene.GetEnvironmentLightingEnabled(),
            resetDockingLayoutRequested,
            commandRouter_);

        if (resetDockingLayoutRequested) {
            workspaceHost_.RequestResetActiveLayout();
        }
        if (const std::optional<EDITOR::EditorWorkspaceActivation> activation =
                workspaceHost_.ApplyPending()) {
            cinematicsWorkspaceController_.ApplyWorkspaceActivation(
                scene,
                *activation,
                context_,
                workspaceHost_);
            modelCollisionWorkspaceController_.ApplyWorkspaceActivation(
                scene,
                *activation,
                workspaceHost_);
            animationStateMachineWorkspaceController_.ApplyWorkspaceActivation(
                scene,
                *activation);
        }

        if (workspaceHost_.IsActive(
                EDITOR::EditorWorkspaceId::AnimationStateMachine)) {
            animationStateMachineWorkspaceController_.DrawDockSpace(
                workspaceHost_.ConsumeReset(
                    EDITOR::EditorWorkspaceId::AnimationStateMachine));
            const EDITOR::AnimationStateMachineWorkspaceResult result =
                animationStateMachineWorkspaceController_.Draw(
                    scene,
                    commandRouter_);
            if (result.exitToSceneRequested) {
                EDITOR::EditorWorkspaceOpenRequest request{};
                request.workspaceId = EDITOR::EditorWorkspaceId::Scene;
                (void)workspaceHost_.RequestOpen(std::move(request));
            }
            if (!result.statusMessage.empty()) {
                viewportDropMessage_ = result.statusMessage;
            }
            DrawPendingSceneOpenModal(scene);
            if (renderQualitySavePending_ && !ImGui::IsAnyItemActive()) {
                (void)SaveRenderQualityProfile(scene);
            }
            return;
        }

        if (workspaceHost_.IsActive(
                EDITOR::EditorWorkspaceId::ModelCollision)) {
            modelCollisionWorkspaceController_.DrawDockSpace(
                workspaceHost_.ConsumeReset(
                    EDITOR::EditorWorkspaceId::ModelCollision));
            const EDITOR::ModelCollisionWorkspaceResult result =
                modelCollisionWorkspaceController_.Draw(
                    scene,
                    workspaceHost_,
                    commandRouter_);
            if (result.exitToSceneRequested) {
                EDITOR::EditorWorkspaceOpenRequest request{};
                request.workspaceId = EDITOR::EditorWorkspaceId::Scene;
                (void)workspaceHost_.RequestOpen(std::move(request));
            }
            if (!result.statusMessage.empty()) {
                viewportDropMessage_ = result.statusMessage;
            }
            DrawPendingSceneOpenModal(scene);
            if (renderQualitySavePending_ && !ImGui::IsAnyItemActive()) {
                (void)SaveRenderQualityProfile(scene);
            }
            return;
        }

        if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Cinematics)) {
            const bool externalDirtyBefore =
                historyExternalDirty_ ||
                ((context_.sceneDirty || scene.HasUnsavedSceneChanges()) &&
                    !documentHistory_.IsDirty());
            std::optional<SceneCinematicsSettings> cinematicsBefore{};
            if (CouldMutateEditorDocumentThisFrame()) {
                cinematicsBefore = scene.GetSceneDocument().cinematics;
            }
            cinematicsWorkspaceController_.DrawDockSpace(
                workspaceHost_.ConsumeReset(
                    EDITOR::EditorWorkspaceId::Cinematics));
            const EDITOR::CinematicsWorkspaceResult result =
                cinematicsWorkspaceController_.Draw(
                    scene,
                    playSession,
                    context_,
                    selectionSync_,
                    workspaceHost_);
            if (result.cinematicsChanged && cinematicsBefore) {
                RecordCinematicsHistory(
                    scene,
                    std::move(*cinematicsBefore),
                    result.timelineEditMergeId,
                    externalDirtyBefore);
            } else if (result.timelineEditMergeId == 0) {
                documentHistory_.SealMerge();
            }
            if (result.saveSceneRequested) {
                (void)commandRouter_.Execute(
                    EDITOR::EditorCommandId::SaveDocument);
            }
            if (result.toggleGamePreviewRequested) {
                ToggleGamePreview(scene, playSession);
            }
            if (!result.statusMessage.empty()) {
                viewportDropMessage_ = result.statusMessage;
            }
            DrawPendingSceneOpenModal(scene);
            if (renderQualitySavePending_ && !ImGui::IsAnyItemActive()) {
                (void)SaveRenderQualityProfile(scene);
            }
            return;
        }

#if defined(HIKARI_WITH_EDITOR)
        DrawEditorDockSpace(
            workspaceHost_.ConsumeReset(EDITOR::EditorWorkspaceId::Scene));
#endif

        if (context_.windows.viewport.showGameView) {
            DrawGameViewportWindow(scene, playSession);
        } else {
            playSession.ReleaseEmbeddedInput();
            viewportTransformHistory_.BeginFrame();
            viewportTransformHistory_.EndFrame(
                scene.GetSceneDocument());
            EDITOR::ClearGameViewportInputRect();
            SERVICES::SetEditorGameViewportSize(0, 0, false);
        }
        if (context_.windows.authoring.showSceneWorkspace) {
            DrawSceneWorkspaceWindow(scene);
        }
        if (context_.windows.authoring.showInspector) {
            DrawInspectorWindow(scene);
        }
        const SceneSystemsPanelResult systemsResult =
            sceneAuthoringUtilityWindows_.Draw(
                scene,
                context_.windows.authoring,
                SERVICES::GetInputService(),
                systemAuthoringRegistry_,
                toolHost_);
        if (systemsResult.changed) {
            historyExternalDirty_ |=
                context_.sceneDirty || scene.HasUnsavedSceneChanges();
            documentHistory_.RecordApplied(
                EDITOR::MakeSceneSystemsHistoryCommand(
                    systemsResult.label,
                    systemsResult.before,
                    scene.GetSceneDocument().systems));
            context_.sceneDirty = true;
            scene.SetUnsavedSceneChanges(true);
            sceneAuthoringUtilityWindows_.SetSystemsRuntimeApplyStatus(
                scene.ApplySystemRuntimeChanges());
        }
        sceneCreationPanel_.DrawDeferredDialogs(
            scene,
            context_,
            selectionSync_);
        sceneInspectorPanel_.DrawDeferredDialogs(
            scene,
            context_,
            selectionSync_,
            sceneObjectCommands_);
        const auto recordSceneObjectHistory =
            [&](std::optional<SceneObjectAuthoringHistoryRequest> history) {
            if (!history) {
                return;
            }
            historyExternalDirty_ |=
                history->dirtyBefore && !documentHistory_.IsDirty();
            documentHistory_.RecordApplied(
                EDITOR::MakeSceneObjectsHistoryCommand(
                    std::move(history->label),
                    std::move(history->beforeObjects),
                    std::move(history->afterObjects),
                    std::move(history->beforeCamera),
                    std::move(history->afterCamera),
                    history->runtimeWorldAffected));
            context_.sceneDirty = true;
            scene.SetUnsavedSceneChanges(true);
        };
        recordSceneObjectHistory(
            sceneCreationPanel_.ConsumeHistoryRequest());
        recordSceneObjectHistory(
            sceneInspectorPanel_.ConsumeHistoryRequest());
        recordSceneObjectHistory(
            viewportTransformHistory_.ConsumeHistoryRequest());
        recordSceneObjectHistory(
            sceneObjectCommands_.ConsumeHistoryRequest());
        if (const std::optional<SceneObjectId> cameraRequest =
                sceneInspectorPanel_.ConsumeOpenCinematicsWorkspaceCameraRequest()) {
            EDITOR::EditorWorkspaceOpenRequest request{};
            request.workspaceId = EDITOR::EditorWorkspaceId::Cinematics;
            request.targetCameraObjectId = *cameraRequest;
            (void)workspaceHost_.RequestOpen(std::move(request));
        }
        if (const std::optional<SceneObjectId> focusRequest =
                sceneInspectorPanel_.ConsumeFocusObjectRequest()) {
            FocusSceneObjects(scene, { *focusRequest });
        }
        if (context_.windows.resources.showAssetBrowser) {
            ProjectSettingsService projectSettings{};
            projectSettings.Load(scene.GetAssetDatabase().GetProjectRoot());
            const ResourceWorkspaceContext resourceContext{
                scene.GetCurrentSceneAssetGuid(),
                projectSettings.GetSettings().startupSceneGuid,
                context_.sceneDirty || scene.HasUnsavedSceneChanges()
            };
            resourceWorkspacePanel_.Draw(
                scene.GetAssetDatabase(),
                scene.GetAssetRegistry(),
                scene.GetSceneDocument(),
                context_.selection,
                resourceContext);

            const std::string activatedModelCollisionGuid =
                resourceWorkspacePanel_.ConsumeActivatedModelCollisionGuid();
            if (!activatedModelCollisionGuid.empty()) {
                EDITOR::EditorWorkspaceOpenRequest request{};
                request.workspaceId =
                    EDITOR::EditorWorkspaceId::ModelCollision;
                request.modelAssetGuid =
                    AssetGuid{ activatedModelCollisionGuid };
                (void)workspaceHost_.RequestOpen(std::move(request));
            }

            const std::string activatedAnimationStateMachineGuid =
                resourceWorkspacePanel_.
                    ConsumeActivatedAnimationStateMachineGuid();
            if (!activatedAnimationStateMachineGuid.empty()) {
                EDITOR::EditorWorkspaceOpenRequest request{};
                request.workspaceId =
                    EDITOR::EditorWorkspaceId::AnimationStateMachine;
                request.animationStateMachineAssetGuid =
                    AssetGuid{ activatedAnimationStateMachineGuid };
                (void)workspaceHost_.RequestOpen(std::move(request));
            }

            const std::string saveSceneAsGuid = resourceWorkspacePanel_.ConsumeSaveSceneAsGuid();
            if (!saveSceneAsGuid.empty()) {
                if (scene.SaveCurrentSceneDocumentAs(AssetGuid{ saveSceneAsGuid })) {
                    context_.sceneDirty = false;
                    scene.SetUnsavedSceneChanges(false);
                    viewportDropMessage_ = "Scene saved to selected asset";
                    HIKARI_LOG_INFO("[SceneAsset] save scene: " + saveSceneAsGuid);
                } else {
                    viewportDropMessage_ = "Scene save target failed";
                    HIKARI_LOG_WARN("[SceneAsset] save scene failed: " + saveSceneAsGuid);
                }
            }

            const std::string activatedSceneGuid = resourceWorkspacePanel_.ConsumeActivatedSceneGuid();
            if (!activatedSceneGuid.empty()) {
                pendingSceneOpenGuid_ = AssetGuid{ activatedSceneGuid };
                if (context_.sceneDirty || scene.HasUnsavedSceneChanges()) {
                    ImGui::OpenPopup("Unsaved Scene Changes");
                } else {
                    OpenSceneAssetFromEditor(scene, pendingSceneOpenGuid_);
                    pendingSceneOpenGuid_ = {};
                }
            }

            RuntimeResourceRefreshService refreshService{};
            auto applyRefreshReport = [this](RuntimeResourceRefreshReport report) {
                context_.lastRuntimeRefreshReport = std::move(report);
                viewportDropMessage_ = SummarizeRuntimeRefreshReport(context_.lastRuntimeRefreshReport);
            };

            if (resourceWorkspacePanel_.ConsumeRefreshCurrentSceneResourcesRequested()) {
                EDITOR::ClearMaterialTextureSlotPreviewCache();
                // 霑ｴ・ｾ陜ｨ・ｨ邵ｺ・ｮ SceneDocument 邵ｺ・ｫ陷・ｽｺ邵ｺ・ｦ邵ｺ荳奇ｽ玖嵩譎擾ｽｭ繝ｻresource 郢ｧ蛛ｵ竏ｪ邵ｺ・ｨ郢ｧ竏壺ｻ陟托ｽｵ郢ｧ鬘泌ｳｩ邵ｺ蜷ｶﾂ繝ｻ
                applyRefreshReport(refreshService.RefreshCurrentSceneResources(scene));
            }

            const std::string refreshRuntimeGuid = resourceWorkspacePanel_.ConsumeRefreshRuntimeAssetGuid();
            if (!refreshRuntimeGuid.empty()) {
                EDITOR::ClearMaterialTextureSlotPreviewCache();
                applyRefreshReport(refreshService.RefreshAsset(scene, AssetId{ refreshRuntimeGuid }));
            }

            const std::string reimportAndRefreshRuntimeGuid =
                resourceWorkspacePanel_.ConsumeReimportAndRefreshRuntimeAssetGuid();
            if (!reimportAndRefreshRuntimeGuid.empty()) {
                EDITOR::ClearMaterialTextureSlotPreviewCache();
                applyRefreshReport(refreshService.RefreshAsset(scene, AssetId{ reimportAndRefreshRuntimeGuid }));
            }

            AssetGuid applyMaterialGuid{};
            PbrMaterialAssetData applyMaterialData{};
            if (resourceWorkspacePanel_.ConsumeApplyRuntimeMaterialRequest(applyMaterialGuid, applyMaterialData)) {
                const int rebuilt = scene.ApplyRuntimeMaterialOverridePreview(
                    applyMaterialGuid,
                    applyMaterialData);
                context_.lastRuntimeRefreshReport = {};
                context_.lastRuntimeRefreshReport.materialReboundComponentCount = rebuilt;
                context_.lastRuntimeRefreshReport.messages.push_back(
                    "Applied material runtime preview: " + applyMaterialGuid.value);
                viewportDropMessage_ = SummarizeRuntimeRefreshReport(context_.lastRuntimeRefreshReport);
            }

            const std::string refreshRuntimeMaterialGuid =
                resourceWorkspacePanel_.ConsumeRefreshRuntimeMaterialGuid();
            if (!refreshRuntimeMaterialGuid.empty()) {
                EDITOR::ClearMaterialTextureSlotPreviewCache();
                applyRefreshReport(refreshService.RefreshAsset(scene, AssetId{ refreshRuntimeMaterialGuid }));
            }
        }
        if (context_.windows.resources.showEnvironment) {
            const EnvironmentPanelResult environmentResult =
                environmentPanel_.Draw(
                    scene.GetSceneEnvironment(),
                    &SKYRENDERER::GetDebugState(),
                    &scene.GetAssetRegistry(),
                    &scene.GetAssetDatabase());
            if (environmentResult.environmentChanged) {
                scene.ApplyEnvironmentRuntimeChanges();
                context_.sceneDirty = true;
            }
            if (environmentResult.openLightingBakeRequested) {
                EDITOR::EditorToolOpenRequest request{};
                request.toolId = EDITOR::kLightingBakeToolId;
                request.target.kind = EDITOR::EditorToolTargetKind::Scene;
                request.target.typeId = "ReflectionProbe";
                request.target.assetGuid =
                    scene.GetCurrentSceneAssetGuid().value;
                (void)toolHost_.RequestOpen(std::move(request));
            }
        }
        if (context_.windows.resources.showQuality) {
            const QualityPanelResult qualityResult =
                qualityPanel_.Draw(scene.GetSceneEnvironment());
            if (qualityResult.environmentChanged) {
                scene.ApplyEnvironmentRuntimeChanges();
                context_.sceneDirty = true;
            }
            renderQualitySavePending_ |= qualityResult.renderQualityChanged;
        }
        EDITOR::EditorToolContext toolContext{ scene, context_ };
        toolHost_.Draw(toolContext);
        if (context_.windows.runtime.showDebugWorkspace) {
            DrawDebugWorkspaceWindow(scene);
        }
        if (context_.windows.runtime.showDebugView) {
            DrawDebugViewWindow(scene, context_.windows.runtime.showDebugView);
        }
        if (context_.windows.runtime.showPerformanceAudit) {
            performanceAuditPanel_.Draw(context_.windows.runtime.showPerformanceAudit);
        }
        if (context_.windows.runtime.showValidationLab) {
            validationLabPanel_.Draw(context_, context_.windows.runtime.showValidationLab);
        }
        DrawPendingSceneOpenModal(scene);
        if (renderQualitySavePending_ && !ImGui::IsAnyItemActive()) {
            (void)SaveRenderQualityProfile(scene);
        }
#else
        (void)scene;
        (void)playSession;
#endif
    }

} // namespace HIKARI
