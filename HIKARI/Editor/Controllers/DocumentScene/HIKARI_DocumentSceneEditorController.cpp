#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include "Core/HIKARI_MathConfig.h"

#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Core/HIKARI_Logger.h"
#include "Editor/AI/HIKARI_AI_ChatClient.h"
#include "Editor/AI/HIKARI_AI_VisionClient.h"
#include "Editor/Authoring/HIKARI_EditorObjectState.h"
#include "Editor/Authoring/HIKARI_EditorObjectFactory.h"
#include "Editor/Assets/Browser/HIKARI_AssetBrowserInternal.h"
#include "Editor/History/HIKARI_EnvironmentHistoryCommand.h"
#include "Editor/History/HIKARI_SceneObjectsHistoryCommand.h"
#include "Editor/History/HIKARI_SceneSystemsHistoryCommand.h"
#include "Editor/Menus/HIKARI_EditorDocumentMenu.h"
#include "Editor/Play/HIKARI_EditorPlaySession.h"
#include "Editor/SystemAuthoring/HIKARI_BuiltInSystemAuthoring.h"
#include "Editor/Tools/HIKARI_BuiltInEditorTools.h"
#include "Editor/Views/HIKARI_EditorViewInstance.h"
#include "Editor/Widgets/HIKARI_MaterialTextureSlotWidget.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialNodeQuery.h"
#include "Editor/HIKARI_EditorViewportInput.h"
#include "Runtime/Presentation/HIKARI_FramePresentationService.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Runtime/HIKARI_RuntimeResourceRefreshService.h"
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

        void SummarizeSceneObjects(
            DocumentSceneBase& scene,
            std::vector<std::string>& outLines,
            size_t limit) {
            const auto& objects = scene.GetWorld().GetObjects();
            size_t count = 0u;
            for (const auto& object : objects) {
                if (count >= limit) {
                    outLines.push_back("...（更多省略）");
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
                outLines.push_back(line);
                ++count;
            }
        }

        bool CapturePreviewToFile(
            std::string& outPath,
            std::string& outError) {
            if (outPath.empty()) {
                const char* temp = std::getenv("TEMP");
                outPath = std::string(temp != nullptr ? temp : ".") +
                    "/hikari-ai-preview.png";
            }
            const std::filesystem::path outputPath(outPath);
            if (outputPath.has_parent_path()) {
                std::error_code errorCode{};
                std::filesystem::create_directories(
                    outputPath.parent_path(), errorCode);
            }
            RENDER3D::EDITORVIEW::RequestPreviewCapture(
                EDITOR::kModelMaterialPreviewRenderViewId,
                outPath);
            for (int attempt = 0; attempt < 100; ++attempt) {
                if (RENDER3D::EDITORVIEW::PollPreviewCapture(
                        outPath, outError)) {
                    return outError.empty();
                }
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(100));
            }
            outError = "截图超时：" + outPath;
            return false;
        }

        void AppendAiHints(
            const std::string& code,
            EDITOR::AI_ASSISTANT::AiRequestResult& result) {
            if (code == "AMBIGUOUS_PARAMETER" ||
                code == "PARAMETER_NOT_FOUND") {
                result.hints = {
                    "请使用完整参数名（如 parameter.baseColor），"
                    "或用 material.graph_query 查看当前参数",
                };
            } else if (code == "PARAMETER_TYPE_MISMATCH") {
                result.hints = {
                    "该参数不是数值类型，无法用 value 修改",
                };
            } else if (code == "UNKNOWN_NODE_TYPE") {
                result.hints = {
                    "请用 help material.graph_edit 查看可用的 "
                    "nodeType 写法（Constant/Parameter/Add 或 "
                    "node.additive 等）",
                };
            } else if (code == "PIN_NOT_FOUND" ||
                       code == "CONNECT_REJECTED") {
                result.hints = {
                    "先用 material.graph_query 查看节点 id 和插槽，"
                    "再连线；插槽可省略或用 out/in 取第一个",
                };
            } else if (code == "FUNCTION_CALL_UNSUPPORTED") {
                result.hints = {
                    "FunctionCall 需要资产 GUID，请在编辑器的 "
                    "Material Functions 菜单添加；也可以用 "
                    "material.graph_edit remove_node 删除坏节点",
                };
            } else if (code == "NODE_NOT_FOUND" ||
                       code == "EDGE_NOT_FOUND") {
                result.hints = {
                    "先用 material.graph_query 查看现有节点 id/"
                    "displayName",
                };
            } else if (code == "SURFACE_OUTPUT_EXISTS" ||
                       code == "SURFACE_OUTPUT_PROTECTED") {
                result.hints = {
                    "Surface Output 是单例根节点，不能重复添加或删除",
                };
            } else if (code.find("COMPILE") != std::string::npos) {
                result.hints = {
                    "请查看 compile 诊断修正节点，"
                    "或用 action.undo_last 回退",
                };
            }
        }

        EDITOR::AI_ASSISTANT::AiRequestResult ExecuteAiSceneAction(
            DocumentSceneBase& scene,
            std::string_view json,
            std::vector<AiSceneUndoRecord>& undoStack) {
            const std::string action =
                EDITOR::AI_ASSISTANT::ExtractJsonField(json, "action");
            if (action != "procedural.create_primitive" &&
                action != "scene.list_objects" &&
                action != "scene.snapshot" &&
                action != "scene.query" &&
                action != "scene.set_transform" &&
                action != "scene.create_object" &&
                action != "asset.list_models" &&
                action != "asset.import" &&
                action != "asset.import_status" &&
                action != "capture.preview") {
                EDITOR::AI_ASSISTANT::AiRequestResult result{};
                result.ok = false;
                result.message = "不支持的场景动作：" + action;
                result.details = {
                    "supported: procedural.create_primitive",
                    "supported: scene.list_objects",
                    "supported: scene.query",
                    "supported: scene.set_transform",
                    "supported: scene.create_object",
                    "supported: asset.list_models",
                    "supported: asset.import",
                    "supported: asset.import_status",
                    "supported: capture.preview",
                };
                return result;
            }

            EDITOR::AI_ASSISTANT::AiRequestResult result{};
            const auto floatField =
                [&json](const char* name, float fallback) {
                    const std::string value =
                        EDITOR::AI_ASSISTANT::ExtractJsonNumberField(
                            json, name);
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

            if (action == "scene.list_objects" ||
                action == "scene.snapshot") {
                result.ok = true;
                result.message = action == "scene.snapshot"
                    ? "场景快照："
                    : "场景对象：";
                SummarizeSceneObjects(scene, result.details, 200u);
                if (result.details.empty()) {
                    result.message = "场景为空";
                }
                return result;
            }

            if (action == "scene.query") {
                const std::string nameFilter =
                    EDITOR::AI_ASSISTANT::ExtractJsonField(
                        json, "name");
                result.ok = true;
                result.message = nameFilter.empty()
                    ? "场景查询："
                    : "场景查询（name 包含 " + nameFilter + "）：";
                size_t count = 0u;
                for (const auto& object :
                     scene.GetWorld().GetObjects()) {
                    if (count >= 200u) {
                        result.details.push_back("...（已截断）");
                        break;
                    }
                    if (!nameFilter.empty() &&
                        object->GetName().find(nameFilter) ==
                            std::string::npos) {
                        continue;
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
                    result.message = "没有匹配的对象";
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
                const MATH::Vec3 previousPosition =
                    target->GetTransform().position;
                const MATH::Vec3 previousScale =
                    target->GetTransform().scale;
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
                        MATH::kDegreesToRadians;
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
                if (changed) {
                    undoStack.push_back(
                        { AiSceneUndoRecord::Kind::Transform,
                          name, previousPosition, previousScale });
                    SummarizeSceneObjects(
                        scene, result.details, 8u);
                }
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
                undoStack.push_back(
                    { AiSceneUndoRecord::Kind::Created,
                      request.name, {}, {} });
                SummarizeSceneObjects(scene, result.details, 8u);
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

            if (action == "asset.import") {
                const std::string sourcePathText =
                    EDITOR::AI_ASSISTANT::ExtractJsonField(
                        json, "path");
                if (sourcePathText.empty()) {
                    result.message = "缺少 path 字段";
                    return result;
                }
                const std::filesystem::path sourcePath(
                    sourcePathText);
                std::error_code fileError;
                if (!std::filesystem::is_regular_file(
                        sourcePath, fileError)) {
                    result.message =
                        "文件不存在或不可读：" + sourcePathText;
                    return result;
                }
                if (!EDITOR::ASSET_BROWSER::IsSupportedImportSource(
                        sourcePath)) {
                    result.message = "不支持的文件类型：" +
                        sourcePath.extension().string();
                    return result;
                }
                AssetDatabase& assetDatabase =
                    scene.GetAssetDatabase();
                const std::filesystem::path targetRelative =
                    EDITOR::ASSET_BROWSER::SuggestedTargetDirectory(
                        {}, sourcePath) /
                    sourcePath.filename();
                std::filesystem::path copiedRelative;
                std::string copyError;
                if (!EDITOR::ASSET_BROWSER::CopySourceFileIntoProject(
                        assetDatabase, sourcePath, targetRelative,
                        false, copiedRelative, copyError)) {
                    result.message = "导入复制失败：" + copyError;
                    return result;
                }
                if (!assetDatabase.ScanAssets(true)) {
                    result.message = "资产扫描失败";
                    return result;
                }
                const AssetRecord* record =
                    assetDatabase.FindByPath(copiedRelative);
                if (record == nullptr) {
                    result.message = "资产注册失败：" +
                        copiedRelative.generic_string();
                    return result;
                }
                const bool queued = assetDatabase.QueueImportAssets(
                    { record->guid }, "AI import");
                result.ok = true;
                result.message = "已复制并注册 " +
                    copiedRelative.generic_string() +
                    (queued
                        ? "，后台导入已排队"
                        : "（已有导入批次运行中）");
                result.details = {
                    "guid: " + record->guid.value,
                    "可用 asset.import_status 查询进度",
                };
                return result;
            }

            if (action == "asset.import_status") {
                const AssetImportBatchStatus status =
                    scene.GetAssetDatabase()
                        .GetQueuedImportStatus();
                result.ok = true;
                result.message = status.active
                    ? "导入进行中"
                    : status.completed
                        ? "导入已完成"
                        : "没有活动导入";
                result.details = {
                    "total: " + std::to_string(status.total),
                    "finished: " +
                        std::to_string(status.finished),
                    "succeeded: " +
                        std::to_string(status.succeeded),
                    "failed: " + std::to_string(status.failed),
                    "label: " + status.label,
                };
                if (!status.message.empty()) {
                    result.details.push_back(
                        "message: " + status.message);
                }
                return result;
            }

            if (action == "capture.preview") {
                std::string path =
                    EDITOR::AI_ASSISTANT::ExtractJsonField(json, "path");
                std::string captureError;
                if (CapturePreviewToFile(path, captureError)) {
                    result.ok = true;
                    result.message = "预览截图已保存：" + path;
                } else {
                    result.message = "截图失败：" + captureError;
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
            undoStack.push_back(
                { AiSceneUndoRecord::Kind::Created,
                  request.object.name, {}, {} });
            SummarizeSceneObjects(scene, result.details, 8u);
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
        modelMaterialWorkspaceController_.SetAiAssistantPanel(
            &aiAssistantPanel_,
            &context_.windows.runtime.showAiAssistant);
    }

    void DocumentSceneEditorController::EnsureAiLocalBridge() {
        if (aiBridgeStarted_) {
            aiAssistantPanel_.SetBridgeStatus(
                aiLocalBridge_.IsRunning()
                    ? "本地桥：" + aiLocalBridge_.GetEndpoint()
                    : "本地桥已停止");
            return;
        }
        aiBridgeStarted_ = true;
        uint16_t port = 18725;
        if (const char* envPort =
                std::getenv("HIKARI_AI_BRIDGE_PORT")) {
            try {
                port = static_cast<uint16_t>(std::stoul(envPort));
            } catch (...) {
            }
        }
        std::string bridgeError;
        if (aiLocalBridge_.Start(
                port,
                [this](std::string_view json) {
                    return aiAssistantPanel_.ExecuteRequest(json);
                },
                bridgeError)) {
            aiAssistantPanel_.SetBridgeStatus(
                "本地桥：" + aiLocalBridge_.GetEndpoint());
        } else {
            aiAssistantPanel_.SetBridgeStatus(
                "本地桥启动失败：" + bridgeError);
        }
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
        aiAssistantPanel_.SetContextCallback([this, &scene]() {
                std::string context =
                    "scene objects: " +
                    std::to_string(
                        scene.GetWorld().GetObjects().size()) +
                    " (";
                size_t shown = 0u;
                for (const auto& object :
                     scene.GetWorld().GetObjects()) {
                    if (shown >= 8u) {
                        context += "...";
                        break;
                    }
                    if (shown > 0u) {
                        context += ", ";
                    }
                    context += object->GetName();
                    ++shown;
                }
            context += ")\n" +
                modelMaterialWorkspaceController_
                    .GetAiMaterialStatus();
            context += "\nmodel assets: " +
                std::to_string(
                    scene.GetAssetRegistry().CollectByType(
                        AssetType::Model).size());
            return context;
        });
            aiAssistantPanel_.SetSceneActionCallback(
                [this, &scene](std::string_view json) {
                    const std::string action =
                        EDITOR::AI_ASSISTANT::ExtractJsonField(json, "action");
                    if (action == "material.edit_parameter") {
                        EDITOR::AI_ASSISTANT::AiRequestResult result{};
                        std::string message;
                        std::string code;
                        const std::string parameter =
                            EDITOR::AI_ASSISTANT::ExtractJsonField(
                                json, "parameter");
                        if (parameter.empty()) {
                            result.message = "缺少 parameter 字段";
                            result.code = "MISSING_FIELD";
                            result.hints = {
                                "请带上完整参数名，例如 "
                                "{\"action\":\"material.edit_parameter\","
                                "\"parameter\":\"parameter.baseColor\","
                                "\"value\":0.8}",
                            };
                            return result;
                        }
                        float value = 0.0f;
                        const std::string valueText =
                            EDITOR::AI_ASSISTANT::ExtractJsonNumberField(
                                json, "value");
                        if (!valueText.empty()) {
                            try {
                                value = std::stof(valueText);
                            } catch (...) {
                                result.message =
                                    "value 不是有效数字：" + valueText;
                                result.code = "INVALID_VALUE";
                                return result;
                            }
                        }
                        if (modelMaterialWorkspaceController_
                                .ApplyAiMaterialParameterEdit(
                                    scene, parameter, value, message,
                                    code)) {
                            result.ok = true;
                            result.code = "OK";
                            result.message = message;
                            modelMaterialWorkspaceController_
                                .AppendAiCompileDiagnostics(
                                    result.details);
                            aiUndoStack_.push_back(
                                { AiSceneUndoRecord::Kind::MaterialEdit,
                                  {}, {}, {} });
                            std::string capturePath;
                            std::string captureError;
                            if (CapturePreviewToFile(
                                    capturePath, captureError)) {
                                result.message +=
                                    " | 截图已保存：" + capturePath;
                            } else {
                                result.message +=
                                    " | 截图失败：" + captureError;
                            }
                        } else {
                            result.message = message;
                            result.code = code;
                            AppendAiHints(code, result);
                        }
                        return result;
                    }
                    if (action == "material.verify") {
                        EDITOR::AI_ASSISTANT::AiRequestResult result{};
                        std::string capturePath;
                        std::string captureError;
                        if (!CapturePreviewToFile(
                                capturePath, captureError)) {
                            result.message =
                                "截图失败：" + captureError;
                            result.code = "CAPTURE_FAILED";
                            return result;
                        }
                        EDITOR::AI_ASSISTANT::AiChatConfig vision{};
                        if (!EDITOR::AI_ASSISTANT::
                                LoadAiVisionConfig(vision)) {
                            result.message =
                                "截图已保存：" + capturePath +
                                "，但未配置视觉 API";
                            result.code = "VISION_NOT_CONFIGURED";
                            result.details = { capturePath };
                            return result;
                        }
                        const std::string prompt =
                            EDITOR::AI_ASSISTANT::ExtractJsonField(
                                json, "prompt");
                        std::string text;
                        std::string error;
                        if (!EDITOR::AI_ASSISTANT::
                                RunVisionAnalysis(
                                    vision,
                                    capturePath,
                                    prompt.empty()
                                        ? "描述这张材质预览，"
                                          "并判断效果是否符合预期"
                                        : prompt,
                                    text,
                                    error)) {
                            result.message =
                                "截图已保存：" + capturePath +
                                "，识图失败：" + error;
                            result.code = "VISION_FAILED";
                            result.details = { capturePath };
                            return result;
                        }
                        result.ok = true;
                        result.code = "OK";
                        result.message = "预览验证：" + text;
                        result.details = { "截图：" + capturePath };
                        return result;
                    }
                    if (action == "material.node_info") {
                        EDITOR::AI_ASSISTANT::AiRequestResult result{};
                        std::string nodeType =
                            EDITOR::AI_ASSISTANT::ExtractJsonField(
                                json, "nodeType");
                        if (nodeType.empty()) {
                            nodeType =
                                EDITOR::AI_ASSISTANT::ExtractJsonField(
                                    json, "name");
                        }
                        if (nodeType.empty()) {
                            result.message =
                                "缺少 nodeType 字段";
                            result.code = "MISSING_FIELD";
                            result.hints = {
                                "请带上节点类型，例如 "
                                "{\"action\":\"material.node_info\","
                                "\"nodeType\":\"noise.value\"}",
                            };
                            return result;
                        }
                        std::string message;
                        std::string code;
                        std::vector<std::string> details;
                        if (EDITOR::DescribeAiMaterialNode(
                                nodeType, message, details, code)) {
                            result.ok = true;
                            result.code = "OK";
                            result.message = message;
                            result.details = std::move(details);
                        } else {
                            result.message = message;
                            result.code = code;
                            result.details = std::move(details);
                            AppendAiHints(code, result);
                        }
                        return result;
                    }
                    if (action == "material.node_list") {
                        EDITOR::AI_ASSISTANT::AiRequestResult result{};
                        const std::string keyword =
                            EDITOR::AI_ASSISTANT::ExtractJsonField(
                                json, "keyword");
                        const std::string nodeClass =
                            EDITOR::AI_ASSISTANT::ExtractJsonField(
                                json, "class");
                        std::string message;
                        std::string code;
                        std::vector<std::string> details;
                        if (EDITOR::ListAiMaterialNodes(
                                keyword, nodeClass,
                                message, details, code)) {
                            result.ok = true;
                            result.code = "OK";
                            result.message = message;
                            result.details = std::move(details);
                        } else {
                            result.message = message;
                            result.code = code;
                            result.details = std::move(details);
                            AppendAiHints(code, result);
                        }
                        return result;
                    }
                    if (action == "material.graph_query") {
                        EDITOR::AI_ASSISTANT::AiRequestResult result{};
                        const std::string nameFilter =
                            EDITOR::AI_ASSISTANT::ExtractJsonField(
                                json, "name");
                        std::string message;
                        std::string code;
                        std::vector<std::string> details;
                        if (modelMaterialWorkspaceController_
                                .QueryAiMaterialGraph(
                                    scene, nameFilter, message,
                                    details, code)) {
                            result.ok = true;
                            result.code = "OK";
                            result.message = message;
                            result.details = std::move(details);
                        } else {
                            result.message = message;
                            result.code = code;
                            AppendAiHints(code, result);
                        }
                        return result;
                    }
                    if (action == "material.graph_layout") {
                        EDITOR::AI_ASSISTANT::AiRequestResult result{};
                        std::string message;
                        std::string code;
                        if (modelMaterialWorkspaceController_
                                .LayoutAiMaterialGraph(
                                    scene, message, code)) {
                            result.ok = true;
                            result.code = "OK";
                            result.message = message;
                            modelMaterialWorkspaceController_
                                .AppendAiCompileDiagnostics(
                                    result.details);
                            aiUndoStack_.push_back(
                                { AiSceneUndoRecord::Kind::MaterialEdit,
                                  {}, {}, {} });
                        } else {
                            result.message = message;
                            result.code = code;
                            AppendAiHints(code, result);
                        }
                        return result;
                    }
                    if (action == "material.graph_edit") {
                        EDITOR::AI_ASSISTANT::AiRequestResult result{};
                        EDITOR::AiMaterialGraphEditRequest request{};
                        request.operation =
                            EDITOR::AI_ASSISTANT::ExtractJsonField(
                                json, "operation");
                        if (request.operation.empty()) {
                            request.operation =
                                EDITOR::AI_ASSISTANT::ExtractJsonField(
                                    json, "op");
                        }
                        request.nodeType =
                            EDITOR::AI_ASSISTANT::ExtractJsonField(
                                json, "nodeType");
                        request.name =
                            EDITOR::AI_ASSISTANT::ExtractJsonField(
                                json, "name");
                        if (request.name.empty()) {
                            request.name =
                                EDITOR::AI_ASSISTANT::ExtractJsonField(
                                    json, "node");
                        }
                        request.sourceNode =
                            EDITOR::AI_ASSISTANT::ExtractJsonField(
                                json, "sourceNode");
                        request.sourcePin =
                            EDITOR::AI_ASSISTANT::ExtractJsonField(
                                json, "sourcePin");
                        request.targetNode =
                            EDITOR::AI_ASSISTANT::ExtractJsonField(
                                json, "targetNode");
                        request.targetPin =
                            EDITOR::AI_ASSISTANT::ExtractJsonField(
                                json, "targetPin");
                        request.edgeId =
                            EDITOR::AI_ASSISTANT::ExtractJsonField(
                                json, "edgeId");
                        request.valueType =
                            EDITOR::AI_ASSISTANT::ExtractJsonField(
                                json, "valueType");
                        request.property =
                            EDITOR::AI_ASSISTANT::ExtractJsonField(
                                json, "property");
                        request.resource =
                            EDITOR::AI_ASSISTANT::ExtractJsonField(
                                json, "resource");
                        request.stringValue =
                            EDITOR::AI_ASSISTANT::ExtractJsonField(
                                json, "value");
                        request.manualPosition =
                            EDITOR::AI_ASSISTANT::ExtractJsonField(
                                json, "layout") == "manual";
                        const auto readFloat =
                            [&json](const char* name, float fallback) {
                                const std::string text =
                                    EDITOR::AI_ASSISTANT::
                                        ExtractJsonNumberField(
                                            json, name);
                                if (text.empty()) {
                                    return fallback;
                                }
                                try {
                                    return std::stof(text);
                                } catch (...) {
                                    return fallback;
                                }
                            };
                        request.positionX =
                            readFloat("positionX", 0.0f);
                        request.positionY =
                            readFloat("positionY", 0.0f);
                        const std::string valueXText =
                            EDITOR::AI_ASSISTANT::ExtractJsonNumberField(
                                json, "valueX");
                        const std::string valueYText =
                            EDITOR::AI_ASSISTANT::ExtractJsonNumberField(
                                json, "valueY");
                        const std::string valueZText =
                            EDITOR::AI_ASSISTANT::ExtractJsonNumberField(
                                json, "valueZ");
                        const std::string valueWText =
                            EDITOR::AI_ASSISTANT::ExtractJsonNumberField(
                                json, "valueW");
                        request.valueSet =
                            !valueXText.empty() ||
                            !valueYText.empty() ||
                            !valueZText.empty() ||
                            !valueWText.empty();
                        request.valueX =
                            readFloat("valueX", 0.0f);
                        request.valueY =
                            readFloat("valueY", 0.0f);
                        request.valueZ =
                            readFloat("valueZ", 0.0f);
                        request.valueW =
                            readFloat("valueW", 0.0f);
                        std::string message;
                        std::string code;
                        std::vector<std::string> graphDetails;
                        if (modelMaterialWorkspaceController_
                                .ApplyAiMaterialGraphEdit(
                                    scene, request, message,
                                    graphDetails, code)) {
                            result.ok = true;
                            result.code = "OK";
                            result.message = message;
                            result.details = std::move(graphDetails);
                            modelMaterialWorkspaceController_
                                .AppendAiCompileDiagnostics(
                                    result.details);
                            aiUndoStack_.push_back(
                                { AiSceneUndoRecord::Kind::MaterialEdit,
                                  {}, {}, {} });
                            std::string capturePath;
                            std::string captureError;
                            if (CapturePreviewToFile(
                                    capturePath, captureError)) {
                                result.message +=
                                    " | 截图已保存：" + capturePath;
                            } else {
                                result.message +=
                                    " | 截图失败：" + captureError;
                            }
                        } else {
                            result.message = message;
                            result.code = code;
                            result.details = std::move(graphDetails);
                            AppendAiHints(code, result);
                        }
                        return result;
                    }
                    if (action == "action.undo_last") {
                        EDITOR::AI_ASSISTANT::AiRequestResult result{};
                        if (aiUndoStack_.empty()) {
                            result.message = "没有可撤销的 AI 操作";
                            return result;
                        }
                        const AiSceneUndoRecord record =
                            aiUndoStack_.back();
                        aiUndoStack_.pop_back();
                        if (record.kind ==
                            AiSceneUndoRecord::Kind::MaterialEdit) {
                            std::string undoMessage;
                            if (modelMaterialWorkspaceController_.Undo(
                                    scene, undoMessage)) {
                                result.ok = true;
                                result.message =
                                    "已撤销上一次材质编辑：" +
                                    undoMessage;
                            } else {
                                result.message =
                                    "材质撤销失败：" + undoMessage;
                            }
                            return result;
                        }
                        GameObject* target = nullptr;
                        for (const auto& object :
                             scene.GetWorld().GetObjects()) {
                            if (object->GetName() == record.name) {
                                target = object.get();
                                break;
                            }
                        }
                        if (record.kind ==
                            AiSceneUndoRecord::Kind::Created) {
                            if (target != nullptr) {
                                scene.GetWorld().DestroyObject(target);
                                result.ok = true;
                                result.message =
                                    "已删除创建的物体：" + record.name;
                            } else {
                                result.message =
                                    "找不到要删除的物体：" + record.name;
                            }
                        } else {
                            if (target != nullptr) {
                                (void)target->SetLocalPosition(
                                    record.position);
                                (void)target->SetLocalScale(
                                    record.scale);
                                result.ok = true;
                                result.message =
                                    "已恢复物体变换：" + record.name;
                            } else {
                                result.message =
                                    "找不到要恢复的物体：" + record.name;
                            }
                        }
                        return result;
                    }
                    return ExecuteAiSceneAction(
                        scene, json, aiUndoStack_);
                });
            EnsureAiLocalBridge();
            if (context_.windows.runtime.showAiAssistant) {
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
