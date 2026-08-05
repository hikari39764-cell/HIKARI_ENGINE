#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Core/HIKARI_Logger.h"
#include "Editor/AI/HIKARI_AI_ChatClient.h"
#include "Editor/Authoring/HIKARI_EditorObjectState.h"
#include "Editor/Authoring/HIKARI_EditorObjectFactory.h"
#include "Editor/History/HIKARI_EnvironmentHistoryCommand.h"
#include "Editor/History/HIKARI_SceneObjectsHistoryCommand.h"
#include "Editor/History/HIKARI_SceneSystemsHistoryCommand.h"
#include "Editor/Menus/HIKARI_EditorDocumentMenu.h"
#include "Editor/Play/HIKARI_EditorPlaySession.h"
#include "Editor/SystemAuthoring/HIKARI_BuiltInSystemAuthoring.h"
#include "Editor/Tools/HIKARI_BuiltInEditorTools.h"
#include "Editor/Widgets/HIKARI_MaterialTextureSlotWidget.h"
#include "Editor/HIKARI_EditorViewportInput.h"
#include "Runtime/Presentation/HIKARI_FramePresentationService.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Runtime/HIKARI_RuntimeResourceRefreshService.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Lighting/HIKARI_SceneLightingSet.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

namespace HIKARI {

    namespace {
        constexpr uint64_t kEnvironmentPanelMergeGroup =
            0x454e5649524f4e4dull;
        constexpr uint64_t kQualityPanelMergeGroup =
            0x5155414c49545945ull;

        std::string SummarizeRuntimeRefreshReport(const RuntimeResourceRefreshReport& report) {
            return "Runtime refresh: texture " + std::to_string(report.textureInvalidatedCount) +
                ", sky " + std::to_string(report.skyInvalidatedCount) +
                ", model " + std::to_string(report.modelReloadedCount) +
                ", rebound " + std::to_string(report.modelReboundComponentCount) +
                ", material " + std::to_string(report.materialReloadedCount) +
                ", mat rebound " + std::to_string(report.materialReboundComponentCount) +
                ", failed " + std::to_string(report.failedCount);
        }

        EDITOR::AI_ASSISTANT::AiRequestResult ExecuteAiSceneAction(
            DocumentSceneBase& scene,
            std::string_view json) {
            const std::string action =
                EDITOR::AI_ASSISTANT::ExtractJsonField(json, "action");
            if (action != "procedural.create_primitive" &&
                action != "scene.list_objects" &&
                action != "scene.set_transform" &&
                action != "scene.create_object" &&
                action != "asset.list_models") {
                EDITOR::AI_ASSISTANT::AiRequestResult result{};
                result.ok = false;
                result.message = "不支持的场景动作：" + action;
                result.details = {
                    "supported: procedural.create_primitive",
                    "supported: scene.list_objects",
                    "supported: scene.set_transform",
                    "supported: scene.create_object",
                    "supported: asset.list_models",
                };
                return result;
            }

            EDITOR::AI_ASSISTANT::AiRequestResult result{};
            const auto floatField =
                [&json](const char* name, float fallback) {
                    const std::string value =
                        EDITOR::AI_ASSISTANT::ExtractJsonField(json, name);
                    if (value.empty()) {
                        return fallback;
                    }
                    try {
                        return std::stof(value);
                    } catch (...) {
                        return fallback;
                    }
                };
            const auto hasField = [&json](const char* name) {
                return json.find(std::string("\"") + name + "\"") !=
                    std::string_view::npos;
            };

            if (action == "scene.list_objects") {
                result.ok = true;
                result.message = "场景对象：";
                const auto& objects = scene.GetWorld().GetObjects();
                size_t count = 0u;
                for (const auto& object : objects) {
                    if (count >= 200u) {
                        result.details.push_back("...（已截断）");
                        break;
                    }
                    const MATH::Vec3& position =
                        object->GetTransform().position;
                    const MATH::Vec3& scale =
                        object->GetTransform().scale;
                    char line[192];
                    std::snprintf(
                        line, sizeof(line),
                        "%s | pos(%.2f, %.2f, %.2f) | "
                        "scale(%.2f, %.2f, %.2f)",
                        object->GetName().c_str(),
                        position.x, position.y, position.z,
                        scale.x, scale.y, scale.z);
                    result.details.push_back(line);
                    ++count;
                }
                if (result.details.empty()) {
                    result.message = "场景为空";
                }
                return result;
            }

            if (action == "scene.set_transform") {
                const std::string name =
                    EDITOR::AI_ASSISTANT::ExtractJsonField(json, "name");
                if (name.empty()) {
                    result.message = "缺少 name 字段（目标物体名）";
                    return result;
                }
                GameObject* target = nullptr;
                for (const auto& object : scene.GetWorld().GetObjects()) {
                    if (object->GetName() == name) {
                        target = object.get();
                        break;
                    }
                }
                if (target == nullptr) {
                    result.message = "找不到物体：" + name;
                    return result;
                }
                bool changed = false;
                if (hasField("positionX") || hasField("positionY") ||
                    hasField("positionZ")) {
                    changed |= target->SetLocalPosition({
                        floatField(
                            "positionX",
                            target->GetTransform().position.x),
                        floatField(
                            "positionY",
                            target->GetTransform().position.y),
                        floatField(
                            "positionZ",
                            target->GetTransform().position.z),
                    });
                }
                if (hasField("scaleX") || hasField("scaleY") ||
                    hasField("scaleZ")) {
                    changed |= target->SetLocalScale({
                        floatField(
                            "scaleX",
                            target->GetTransform().scale.x),
                        floatField(
                            "scaleY",
                            target->GetTransform().scale.y),
                        floatField(
                            "scaleZ",
                            target->GetTransform().scale.z),
                    });
                }
                if (hasField("rotationX") || hasField("rotationY") ||
                    hasField("rotationZ")) {
                    constexpr float kDegreesToRadians =
                        0.0174532925f;
                    changed |= target->SetLocalRotation(
                        MATH::Quat::FromEulerXYZ(
                            floatField("rotationX", 0.0f) *
                                kDegreesToRadians,
                            floatField("rotationY", 0.0f) *
                                kDegreesToRadians,
                            floatField("rotationZ", 0.0f) *
                                kDegreesToRadians));
                }
                result.ok = true;
                result.message = changed
                    ? "已更新物体 " + name
                    : "物体 " + name +
                        " 未变化（需要 positionX/scaleX/rotationX 等字段）";
                return result;
            }

            if (action == "scene.create_object") {
                const std::string model =
                    EDITOR::AI_ASSISTANT::ExtractJsonField(json, "model");
                if (model.empty()) {
                    result.message =
                        "缺少 model 字段（资产 id 或路径片段，"
                        "可用 asset.list_models 查询）";
                    return result;
                }
                const AssetRegistry& registry =
                    scene.GetAssetRegistry();
                const AssetDescriptor* descriptor =
                    registry.FindDescriptor(model);
                if (descriptor == nullptr) {
                    for (const AssetDescriptor* candidate :
                         registry.CollectByType(AssetType::Model)) {
                        if (candidate->sourcePath.find(model) !=
                                std::string::npos ||
                            candidate->id.value.find(model) !=
                                std::string::npos) {
                            descriptor = candidate;
                            break;
                        }
                    }
                }
                if (descriptor == nullptr) {
                    result.message = "找不到模型资产：" + model;
                    return result;
                }
                EDITOR::CreateObjectRequest request{};
                request.name =
                    EDITOR::AI_ASSISTANT::ExtractJsonField(json, "name");
                if (request.name.empty()) {
                    request.name = "AI Model";
                }
                request.position = {
                    floatField("positionX", 0.0f),
                    floatField("positionY", 0.0f),
                    floatField("positionZ", 0.0f),
                };
                request.scale = {
                    floatField("scaleX", 1.0f),
                    floatField("scaleY", 1.0f),
                    floatField("scaleZ", 1.0f),
                };
                GameObject* object = EDITOR::CreateModelObject(
                    scene,
                    AssetGuid{ descriptor->id.value },
                    request);
                if (object == nullptr) {
                    result.message = "创建模型物体失败：" + request.name;
                    return result;
                }
                result.ok = true;
                result.message = "已创建模型 " + request.name +
                    "（" + descriptor->sourcePath + "）";
                return result;
            }

            if (action == "asset.list_models") {
                result.ok = true;
                result.message = "模型资产：";
                for (const AssetDescriptor* descriptor :
                     scene.GetAssetRegistry().CollectByType(
                         AssetType::Model)) {
                    result.details.push_back(
                        descriptor->id.value + " | " +
                        descriptor->sourcePath);
                }
                if (result.details.empty()) {
                    result.message = "没有已注册的模型资产";
                }
                return result;
            }

            const std::string kind =
                EDITOR::AI_ASSISTANT::ExtractJsonField(json, "kind");
            ProceduralMeshKind meshKind = ProceduralMeshKind::Box;
            if (kind == "sphere") {
                meshKind = ProceduralMeshKind::Sphere;
            } else if (kind == "plane") {
                meshKind = ProceduralMeshKind::Plane;
            } else if (kind == "grid_plane") {
                meshKind = ProceduralMeshKind::GridPlane;
            } else if (kind == "cylinder") {
                meshKind = ProceduralMeshKind::Cylinder;
            } else if (kind == "capsule") {
                meshKind = ProceduralMeshKind::Capsule;
            }

            EDITOR::CreatePrimitiveRequest request{};
            request.object.name =
                EDITOR::AI_ASSISTANT::ExtractJsonField(json, "name");
            if (request.object.name.empty()) {
                request.object.name =
                    kind.empty() ? "AI Primitive" : kind;
            }
            request.object.position = {
                floatField("positionX", 0.0f),
                floatField("positionY", 0.0f),
                floatField("positionZ", 0.0f),
            };
            request.object.scale = {
                floatField("scaleX", 1.0f),
                floatField("scaleY", 1.0f),
                floatField("scaleZ", 1.0f),
            };
            request.mesh.kind = meshKind;
            request.mesh.width = floatField("width", 1.0f);
            request.mesh.height = floatField("height", 1.0f);
            request.mesh.depth = floatField("depth", 1.0f);
            request.mesh =
                SanitizeProceduralMeshSettings(request.mesh);
            GameObject* object =
                EDITOR::CreatePrimitiveObject(scene, request);
            if (object == nullptr) {
                result.message =
                    "创建程序化物体失败：" + request.object.name;
                return result;
            }
            char positionText[96];
            std::snprintf(
                positionText, sizeof(positionText),
                "(%.2f, %.2f, %.2f)",
                request.object.position.x,
                request.object.position.y,
                request.object.position.z);
            result.ok = true;
            result.message =
                "已创建 " + request.object.name + " " +
                std::string(positionText);
            return result;
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
            modelMaterialWorkspaceController_.ApplyWorkspaceActivation(
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
                EDITOR::EditorWorkspaceId::ModelMaterial)) {
            modelMaterialWorkspaceController_.DrawDockSpace(
                workspaceHost_.ConsumeReset(
                    EDITOR::EditorWorkspaceId::ModelMaterial));
            const EDITOR::ModelMaterialWorkspaceResult result =
                modelMaterialWorkspaceController_.Draw(
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
            const bool externalDirtyBefore =
                historyExternalDirty_ ||
                ((context_.sceneDirty ||
                    scene.HasUnsavedSceneChanges()) &&
                    !documentHistory_.IsDirty());
            CommitAppliedDocumentCommand(
                scene,
                EDITOR::MakeSceneSystemsHistoryCommand(
                    systemsResult.label,
                    systemsResult.before,
                    scene.GetSceneDocument().systems),
                0,
                externalDirtyBefore,
                EDITOR::EditorDocumentImpact::Systems);
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
            EDITOR::EditorDocumentImpact impact =
                history->runtimeWorldAffected
                ? EDITOR::EditorDocumentImpact::RuntimeWorld
                : EDITOR::EditorDocumentImpact::None;
            const auto beforeLighting =
                LIGHTING_AUTHORING::BuildSceneLightingSet(
                    std::span<const SceneObjectData>(
                        history->beforeObjects));
            const auto afterLighting =
                LIGHTING_AUTHORING::BuildSceneLightingSet(
                    std::span<const SceneObjectData>(
                        history->afterObjects));
            const bool lightingChanged =
                !LIGHTING_AUTHORING::
                    AreSceneLightingSetsEquivalent(
                        beforeLighting,
                        afterLighting);
            if (lightingChanged) {
                impact = impact |
                    EDITOR::EditorDocumentImpact::Lighting;
            }
            CommitAppliedDocumentCommand(
                scene,
                EDITOR::MakeSceneObjectsHistoryCommand(
                    std::move(history->label),
                    std::move(history->beforeObjects),
                    std::move(history->afterObjects),
                    std::move(history->beforeCamera),
                    std::move(history->afterCamera),
                    impact),
                0,
                history->dirtyBefore &&
                    !documentHistory_.IsDirty(),
                lightingChanged
                    ? EDITOR::EditorDocumentImpact::Lighting
                    : EDITOR::EditorDocumentImpact::None);
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
        bool environmentEditActive = false;
        if (context_.windows.resources.showEnvironment) {
            const SceneEnvironment beforeEnvironment =
                scene.GetSceneDocument().environment;
            SceneEnvironment editedEnvironment =
                beforeEnvironment;
            const EnvironmentPanelResult environmentResult =
                environmentPanel_.Draw(
                    editedEnvironment,
                    &SKYRENDERER::GetDebugState(),
                    &scene.GetAssetRegistry(),
                    &scene.GetAssetDatabase());
            environmentEditActive |= environmentResult.editActive;
            if (environmentResult.environmentChanged) {
                const bool externalDirtyBefore =
                    historyExternalDirty_ ||
                    ((context_.sceneDirty ||
                        scene.HasUnsavedSceneChanges()) &&
                        !documentHistory_.IsDirty());
                scene.GetSceneDocument().environment =
                    editedEnvironment;
                CommitAppliedDocumentCommand(
                    scene,
                    EDITOR::MakeEnvironmentHistoryCommand(
                        "Edit Environment",
                        beforeEnvironment,
                        std::move(editedEnvironment)),
                    environmentResult.editActive
                        ? kEnvironmentPanelMergeGroup
                        : 0u,
                    externalDirtyBefore,
                    EDITOR::EditorDocumentImpact::Environment);
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
            const SceneEnvironment beforeEnvironment =
                scene.GetSceneDocument().environment;
            SceneEnvironment editedEnvironment =
                beforeEnvironment;
            const QualityPanelResult qualityResult =
                qualityPanel_.Draw(editedEnvironment);
            environmentEditActive |= qualityResult.editActive;
            if (qualityResult.environmentChanged) {
                const bool externalDirtyBefore =
                    historyExternalDirty_ ||
                    ((context_.sceneDirty ||
                        scene.HasUnsavedSceneChanges()) &&
                        !documentHistory_.IsDirty());
                scene.GetSceneDocument().environment =
                    editedEnvironment;
                CommitAppliedDocumentCommand(
                    scene,
                    EDITOR::MakeEnvironmentHistoryCommand(
                        "Edit Quality Environment",
                        beforeEnvironment,
                        std::move(editedEnvironment)),
                    qualityResult.editActive
                        ? kQualityPanelMergeGroup
                        : 0u,
                    externalDirtyBefore,
                    EDITOR::EditorDocumentImpact::Environment);
            }
            renderQualitySavePending_ |= qualityResult.renderQualityChanged;
        }
        if (!environmentEditActive) {
            documentHistory_.SealMerge();
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
        if (context_.windows.runtime.showAiAssistant) {
            aiAssistantPanel_.SetSceneActionCallback(
                [&scene](std::string_view json) {
                    return ExecuteAiSceneAction(scene, json);
                });
            aiAssistantPanel_.Draw(context_.windows.runtime.showAiAssistant);
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
