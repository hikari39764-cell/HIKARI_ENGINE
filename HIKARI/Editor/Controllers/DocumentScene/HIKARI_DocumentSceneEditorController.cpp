#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include "Core/HIKARI_MathConfig.h"
#include "Core/Text/HIKARI_Localization.h"

#include <algorithm>
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
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsOperations.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialOperations.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialNodeQuery.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionOperations.h"
#include "Editor/Workspaces/AnimationStateMachine/HIKARI_AnimationStateMachineOperations.h"
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

#include <json.hpp>

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

        float ReadJsonFloat(
            const nlohmann::json& json,
            const char* key,
            float fallback) {
            const auto found = json.find(key);
            if (found == json.end()) {
                return fallback;
            }
            if (found->is_number()) {
                return found->get<float>();
            }
            if (found->is_string()) {
                try {
                    return std::stof(found->get<std::string>());
                } catch (...) {
                    return fallback;
                }
            }
            return fallback;
        }

        EDITOR::EditorOperationParameter MakeOperationParameter(
            std::string name,
            EDITOR::EditorOperationParameterType type,
            bool required,
            std::string description) {
            EDITOR::EditorOperationParameter parameter{};
            parameter.name = std::move(name);
            parameter.type = type;
            parameter.required = required;
            parameter.description = std::move(description);
            return parameter;
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
            EDITOR::EditorOperationResult& result) {
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

        EDITOR::AI_ASSISTANT::AiRequestResult ToAiRequestResult(
            const EDITOR::EditorOperationResult& operation) {
            EDITOR::AI_ASSISTANT::AiRequestResult result{};
            result.ok = operation.ok;
            result.code = operation.code;
            result.message = operation.message;
            result.details = operation.details;
            result.hints = operation.hints;
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
            // v2: default layout only docks windows that are real dockable
            // panels. The new ID intentionally ignores stale imgui.ini nodes.
            const ImGuiID dockspaceId =
                ImGui::GetID("HIKARI_EditorDockSpace_v2");
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
                ImGui::DockBuilderDockWindow("AI Assistant", rightNode);
                ImGui::DockBuilderDockWindow("Resource Workspace", bottomNode);
                ImGui::DockBuilderDockWindow("Environment", bottomNode);
                ImGui::DockBuilderDockWindow("Quality", bottomNode);
                ImGui::DockBuilderDockWindow("Debug View", bottomNode);
                ImGui::DockBuilderDockWindow("Performance Audit", bottomNode);

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
        aiAssistantPanel_.SetCapabilityProvider(
            [this]() {
                std::vector<EDITOR::AI_ASSISTANT::AiCapability>
                    capabilities =
                        EDITOR::AI_ASSISTANT::BuiltInCapabilities();
                for (const std::string& id :
                     operationRegistry_.RegisteredIds()) {
                    const EDITOR::EditorOperationDefinition* definition =
                        operationRegistry_.Find(id);
                    if (definition == nullptr) {
                        continue;
                    }
                    capabilities.push_back(
                        { definition->id,
                          definition->summary,
                          "stable" });
                }
                std::sort(
                    capabilities.begin(),
                    capabilities.end(),
                    [](const EDITOR::AI_ASSISTANT::AiCapability& lhs,
                       const EDITOR::AI_ASSISTANT::AiCapability& rhs) {
                        return lhs.id < rhs.id;
                    });
                return capabilities;
            });
        aiLocalBridge_.SetCapabilityProvider(
            [this]() {
                return aiAssistantPanel_.Capabilities();
            });
        aiAssistantPanel_.SetWorkerExecutionClassifier(
            [this](std::string_view action) {
                const EDITOR::EditorOperationDefinition* definition =
                    operationRegistry_.Find(std::string(action));
                return definition != nullptr &&
                    definition->executionPolicy ==
                        EDITOR::EditorOperationExecutionPolicy::WorkerThread;
            });
        modelMaterialWorkspaceController_.SetOperationRegistry(
            &operationRegistry_);
        animationStateMachineWorkspaceController_.SetOperationRegistry(
            &operationRegistry_);
        using EDITOR::EditorOperationParameterType;
        const auto P = MakeOperationParameter;
        operationRegistry_.Register({
            "material.node_info",
            "Return one node's semantic contract.",
            "model_material",
            {
                P("nodeType", EditorOperationParameterType::String,
                  false, "稳定节点类型名，例如 node.additive"),
                P("name", EditorOperationParameterType::String,
                  false, "节点类型别名，与 nodeType 二选一"),
            },
            [](const nlohmann::json& parameters)
                -> EDITOR::EditorOperationResult {
                std::string nodeType =
                    parameters.value("nodeType", std::string{});
                if (nodeType.empty()) {
                    nodeType =
                        parameters.value("name", std::string{});
                }
                EDITOR::EditorOperationResult result{};
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
                    result.code = code;
                    result.message = message;
                    result.details = std::move(details);
                }
                return result;
            },
        });
        operationRegistry_.Register({
            "scene.set_transform",
            "Set an existing object's position/rotation/scale by name.",
            "scene",
            {
                P("name", EditorOperationParameterType::String,
                  true, "目标物体名称"),
                P("positionX", EditorOperationParameterType::Float,
                  false, "世界位置 X"),
                P("positionY", EditorOperationParameterType::Float,
                  false, "世界位置 Y"),
                P("positionZ", EditorOperationParameterType::Float,
                  false, "世界位置 Z"),
                P("scaleX", EditorOperationParameterType::Float,
                  false, "缩放 X"),
                P("scaleY", EditorOperationParameterType::Float,
                  false, "缩放 Y"),
                P("scaleZ", EditorOperationParameterType::Float,
                  false, "缩放 Z"),
                P("rotationX", EditorOperationParameterType::Float,
                  false, "欧拉旋转 X（度）"),
                P("rotationY", EditorOperationParameterType::Float,
                  false, "欧拉旋转 Y（度）"),
                P("rotationZ", EditorOperationParameterType::Float,
                  false, "欧拉旋转 Z（度）"),
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteAiSceneSetTransform(parameters);
            },
        });
        operationRegistry_.Register({
            "scene.list_objects",
            "List scene objects with position and scale.",
            "scene",
            {},
            [this](const nlohmann::json&) {
                return ExecuteAiSceneObjectQuery(false);
            },
        });
        operationRegistry_.Register({
            "scene.snapshot",
            "Return a compact scene object snapshot.",
            "scene",
            {},
            [this](const nlohmann::json&) {
                return ExecuteAiSceneObjectQuery(true);
            },
        });
        operationRegistry_.Register({
            "scene.query",
            "Query scene objects by name filter.",
            "scene",
            {
                P("name", EditorOperationParameterType::String,
                  false, "按名称包含过滤"),
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteAiSceneQuery(parameters);
            },
        });
        operationRegistry_.Register({
            "asset.list_models",
            "List registered model assets.",
            "assets",
            {},
            [this](const nlohmann::json&) {
                return ExecuteAiAssetListModels();
            },
        });
        operationRegistry_.Register({
            "capture.preview",
            "Capture the material preview to a file.",
            "general",
            {
                P("path", EditorOperationParameterType::String,
                  false, "输出文件路径，缺省为临时目录"),
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteAiCapturePreview(parameters);
            },
            EDITOR::EditorOperationExecutionPolicy::WorkerThread,
        });
        operationRegistry_.Register({
            "material.graph_query",
            "List material graph nodes with ids, pins and connections.",
            "model_material",
            {
                P("name", EditorOperationParameterType::String,
                  false, "按节点 id/displayName 过滤"),
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteAiMaterialGraphQuery(parameters);
            },
        });
        operationRegistry_.Register({
            "material.graph_layout",
            "Auto-layout material graph nodes.",
            "model_material",
            {},
            [this](const nlohmann::json&) {
                return ExecuteAiMaterialGraphLayout();
            },
        });
        operationRegistry_.Register({
            "asset.import",
            "Copy a supported source file into the project and queue its import.",
            "assets",
            {
                P("path", EditorOperationParameterType::String,
                  true, "源文件绝对路径"),
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteAiAssetImport(parameters);
            },
        });
        operationRegistry_.Register({
            "asset.import_status",
            "Report the current queued asset import progress.",
            "assets",
            {},
            [this](const nlohmann::json&) {
                return ExecuteAiAssetImportStatus();
            },
        });
        operationRegistry_.Register({
            "action.undo_last",
            "Undo the most recent edit in the active workspace.",
            "general",
            {},
            [this](const nlohmann::json&) {
                return ExecuteAiUndoLast();
            },
        });
        operationRegistry_.Register({
            "material.node_list",
            "List discoverable material graph node types.",
            "model_material",
            {
                P("keyword", EditorOperationParameterType::String,
                  false, "按关键字过滤节点类型"),
                P("class", EditorOperationParameterType::String,
                  false, "按节点类别过滤"),
            },
            [](const nlohmann::json& parameters) {
                EDITOR::EditorOperationResult result{};
                std::string message;
                std::string code;
                std::vector<std::string> details;
                if (EDITOR::ListAiMaterialNodes(
                        parameters.value("keyword", std::string{}),
                        parameters.value("class", std::string{}),
                        message, details, code)) {
                    result.ok = true;
                    result.code = "OK";
                    result.message = message;
                    result.details = std::move(details);
                } else {
                    result.code = code;
                    result.message = message;
                    result.details = std::move(details);
                }
                return result;
            },
        });
        operationRegistry_.Register({
            "scene.create_object",
            "Create a model object in the scene by asset id or path fragment.",
            "scene",
            {
                P("model", EditorOperationParameterType::String,
                  true, "模型资产 id 或路径片段"),
                P("name", EditorOperationParameterType::String,
                  false, "物体名称，缺省为 AI Model"),
                P("positionX", EditorOperationParameterType::Float,
                  false, "世界位置 X"),
                P("positionY", EditorOperationParameterType::Float,
                  false, "世界位置 Y"),
                P("positionZ", EditorOperationParameterType::Float,
                  false, "世界位置 Z"),
                P("scaleX", EditorOperationParameterType::Float,
                  false, "缩放 X"),
                P("scaleY", EditorOperationParameterType::Float,
                  false, "缩放 Y"),
                P("scaleZ", EditorOperationParameterType::Float,
                  false, "缩放 Z"),
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteAiSceneCreateObject(parameters);
            },
        });
        operationRegistry_.Register({
            "procedural.create_primitive",
            "Create a procedural mesh object (box/sphere/plane/grid_plane/cylinder/capsule).",
            "scene",
            {
                P("kind", EditorOperationParameterType::String,
                  false, "box/sphere/plane/grid_plane/cylinder/capsule"),
                P("name", EditorOperationParameterType::String,
                  false, "物体名称，缺省使用 kind"),
                P("positionX", EditorOperationParameterType::Float,
                  false, "世界位置 X"),
                P("positionY", EditorOperationParameterType::Float,
                  false, "世界位置 Y"),
                P("positionZ", EditorOperationParameterType::Float,
                  false, "世界位置 Z"),
                P("scaleX", EditorOperationParameterType::Float,
                  false, "缩放 X"),
                P("scaleY", EditorOperationParameterType::Float,
                  false, "缩放 Y"),
                P("scaleZ", EditorOperationParameterType::Float,
                  false, "缩放 Z"),
                P("width", EditorOperationParameterType::Float,
                  false, "网格宽度"),
                P("height", EditorOperationParameterType::Float,
                  false, "网格高度"),
                P("depth", EditorOperationParameterType::Float,
                  false, "网格深度"),
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteAiProceduralCreatePrimitive(parameters);
            },
        });
        operationRegistry_.Register({
            "material.edit_parameter",
            "Edit a scalar material graph parameter by full parameter name.",
            "model_material",
            {
                P("parameter", EditorOperationParameterType::String,
                  true, "完整参数名，例如 parameter.baseColor"),
                P("value", EditorOperationParameterType::Float,
                  true, "要写入的数值"),
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteAiMaterialEditParameter(parameters);
            },
        });
        operationRegistry_.Register({
            "material.graph_edit",
            "Add/remove/connect material graph nodes and edit node values.",
            "model_material",
            {
                P("operation", EditorOperationParameterType::String,
                  true, "add_node/remove_node/connect/disconnect/set_node"),
                P("nodeType", EditorOperationParameterType::String,
                  false, "add_node 的稳定节点类型名"),
                P("name", EditorOperationParameterType::String,
                  false, "add_node 的显示名或 remove_node 的节点 id/name"),
                P("node", EditorOperationParameterType::String,
                  false, "name 的别名"),
                P("sourceNode", EditorOperationParameterType::String,
                  false, "连线起点节点"),
                P("sourcePin", EditorOperationParameterType::String,
                  false, "连线起点插槽，缺省取第一个"),
                P("targetNode", EditorOperationParameterType::String,
                  false, "连线终点节点"),
                P("targetPin", EditorOperationParameterType::String,
                  false, "连线终点插槽，缺省取第一个"),
                P("edgeId", EditorOperationParameterType::String,
                  false, "disconnect 的边 id"),
                P("valueType", EditorOperationParameterType::String,
                  false, "set_node 的值类型"),
                P("property", EditorOperationParameterType::String,
                  false, "set_node 的属性名"),
                P("resource", EditorOperationParameterType::String,
                  false, "set_node 的资源 id（TextureSample）"),
                P("value", EditorOperationParameterType::String,
                  false, "set_node 的字符串属性值"),
                P("layout", EditorOperationParameterType::String,
                  false, "manual 保留坐标，否则自动布局"),
                P("positionX", EditorOperationParameterType::Float,
                  false, "节点 X"),
                P("positionY", EditorOperationParameterType::Float,
                  false, "节点 Y"),
                P("valueX", EditorOperationParameterType::Float,
                  false, "值 X"),
                P("valueY", EditorOperationParameterType::Float,
                  false, "值 Y"),
                P("valueZ", EditorOperationParameterType::Float,
                  false, "值 Z"),
                P("valueW", EditorOperationParameterType::Float,
                  false, "值 W"),
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteAiMaterialGraphEdit(parameters);
            },
        });
        operationRegistry_.Register({
            "material.verify",
            "Capture the material preview and run a vision analysis on it.",
            "model_material",
            {
                P("prompt", EditorOperationParameterType::String,
                  false, "识图提示词，缺省使用默认描述"),
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteAiMaterialVerify(parameters);
            },
            EDITOR::EditorOperationExecutionPolicy::WorkerThread,
        });
        operationRegistry_.Register({
            "scene.save",
            "Save the current scene document.",
            "scene",
            {},
            [this](const nlohmann::json&) {
                return ExecuteSceneSave();
            },
        });
        operationRegistry_.Register({
            "scene.delete_object",
            "Delete the selected object, or the object with the given name.",
            "scene",
            {
                P("name", EditorOperationParameterType::String,
                  false, "物体名称；缺省删除当前选中对象"),
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteSceneDeleteObject(parameters);
            },
        });
        operationRegistry_.Register({
            "scene.duplicate_object",
            "Duplicate the selected object, or the object with the given name.",
            "scene",
            {
                P("name", EditorOperationParameterType::String,
                  false, "物体名称；缺省复制当前选中对象"),
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteSceneDuplicateObject(parameters);
            },
        });
        operationRegistry_.Register({
            "scene.create_empty_object",
            "Create an empty scene object.",
            "scene",
            {
                P("name", EditorOperationParameterType::String,
                  false, "物体名称，缺省为 AI Object"),
                P("positionX", EditorOperationParameterType::Float,
                  false, "世界位置 X"),
                P("positionY", EditorOperationParameterType::Float,
                  false, "世界位置 Y"),
                P("positionZ", EditorOperationParameterType::Float,
                  false, "世界位置 Z"),
                P("scaleX", EditorOperationParameterType::Float,
                  false, "缩放 X"),
                P("scaleY", EditorOperationParameterType::Float,
                  false, "缩放 Y"),
                P("scaleZ", EditorOperationParameterType::Float,
                  false, "缩放 Z"),
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteSceneCreateEmptyObject(parameters);
            },
        });
        EDITOR::RegisterModelCollisionOperations(
            operationRegistry_,
            modelCollisionWorkspaceController_,
            [this]() { return aiOperationScene_; });
        EDITOR::RegisterAnimationStateMachineOperations(
            operationRegistry_,
            animationStateMachineWorkspaceController_,
            [this]() { return aiOperationScene_; });
        EDITOR::RegisterCinematicsOperations(
            operationRegistry_,
            cinematicsWorkspaceController_,
            [this]() { return aiOperationScene_; });
        EDITOR::RegisterModelMaterialOperations(
            operationRegistry_,
            modelMaterialWorkspaceController_,
            [this]() { return aiOperationScene_; });
        commandRouter_.SetOperationRegistry(&operationRegistry_);
    }

    DocumentSceneEditorController::~DocumentSceneEditorController() {
        aiAssistantPanel_.Stop();
        aiLocalBridge_.Stop();
    }

    EDITOR::EditorOperationResult
    DocumentSceneEditorController::ExecuteAiSceneSetTransform(
        const nlohmann::json& parameters) {
        EDITOR::EditorOperationResult result{};
        DocumentSceneBase* scene = aiOperationScene_;
        if (scene == nullptr) {
            result.code = "NO_SCENE";
            result.message = "场景上下文不可用";
            return result;
        }
        const std::string name =
            parameters.value("name", std::string{});
        if (name.empty()) {
            result.code = "MISSING_FIELD";
            result.message = "缺少 name 字段（目标物体名）";
            return result;
        }
        GameObject* target = nullptr;
        for (const auto& object : scene->GetWorld().GetObjects()) {
            if (object->GetName() == name) {
                target = object.get();
                break;
            }
        }
        if (target == nullptr) {
            result.code = "OBJECT_NOT_FOUND";
            result.message = "找不到物体：" + name;
            return result;
        }
        SceneObjectData* documentObject =
            selectionSync_.FindDocumentObjectByRuntime(
                *scene, target);
        if (documentObject == nullptr) {
            result.code = "OBJECT_NOT_IN_DOCUMENT";
            result.message =
                "目标物体不在场景文档中，不能安全修改";
            return result;
        }
        const MATH::Vec3 beforePosition =
            documentObject->transform.position;
        const MATH::Vec3 beforeScale =
            documentObject->transform.scale;
        const MATH::Vec3 beforeRotation =
            documentObject->transform.rotationEulerDeg;
        const auto hasField = [&parameters](const char* key) {
            return parameters.contains(key);
        };
        const auto floatField =
            [&parameters](const char* key, float fallback) {
                const auto found = parameters.find(key);
                return found != parameters.end() &&
                        found->is_number()
                    ? found->get<float>()
                    : fallback;
            };
        const auto vec3Changed =
            [](const MATH::Vec3& lhs, const MATH::Vec3& rhs) {
                return lhs.x != rhs.x || lhs.y != rhs.y ||
                    lhs.z != rhs.z;
            };
        bool changed = false;
        if (hasField("positionX") || hasField("positionY") ||
            hasField("positionZ")) {
            documentObject->transform.position = {
                floatField(
                    "positionX",
                    documentObject->transform.position.x),
                floatField(
                    "positionY",
                    documentObject->transform.position.y),
                floatField(
                    "positionZ",
                    documentObject->transform.position.z),
            };
            changed = vec3Changed(
                beforePosition,
                documentObject->transform.position);
        }
        if (hasField("scaleX") || hasField("scaleY") ||
            hasField("scaleZ")) {
            documentObject->transform.scale = {
                floatField(
                    "scaleX",
                    documentObject->transform.scale.x),
                floatField(
                    "scaleY",
                    documentObject->transform.scale.y),
                floatField(
                    "scaleZ",
                    documentObject->transform.scale.z),
            };
            changed |= vec3Changed(
                beforeScale,
                documentObject->transform.scale);
        }
        if (hasField("rotationX") || hasField("rotationY") ||
            hasField("rotationZ")) {
            documentObject->transform.rotationEulerDeg = {
                floatField(
                    "rotationX",
                    documentObject->transform.rotationEulerDeg.x),
                floatField(
                    "rotationY",
                    documentObject->transform.rotationEulerDeg.y),
                floatField(
                    "rotationZ",
                    documentObject->transform.rotationEulerDeg.z),
            };
            changed |= vec3Changed(
                beforeRotation,
                documentObject->transform.rotationEulerDeg);
        }
        if (changed) {
            Transform3D runtime = target->GetTransform();
            runtime.position =
                documentObject->transform.position;
            runtime.scale = documentObject->transform.scale;
            runtime.rotation = MATH::Quat::FromEulerXYZ(
                documentObject->transform.rotationEulerDeg.x *
                    MATH::kDegreesToRadians,
                documentObject->transform.rotationEulerDeg.y *
                    MATH::kDegreesToRadians,
                documentObject->transform.rotationEulerDeg.z *
                    MATH::kDegreesToRadians);
            runtime.useExplicitMatrix = false;
            (void)target->SetLocalTransform(runtime);
        }
        result.ok = true;
        if (changed) {
            const bool dirtyBefore =
                context_.sceneDirty ||
                scene->HasUnsavedSceneChanges();
            const std::vector<SceneObjectData> beforeObjects =
                scene->GetSceneDocument().objects;
            const SceneCameraSettings beforeCamera =
                scene->GetSceneDocument().camera;
            CommitSceneObjectAuthoringHistory(
                *scene,
                SceneObjectAuthoringHistoryRequest{
                    "Edit Transform",
                    beforeObjects,
                    scene->GetSceneDocument().objects,
                    beforeCamera,
                    scene->GetSceneDocument().camera,
                    dirtyBefore });
            SummarizeSceneObjects(
                *scene, result.details, 8u);
        }
        result.message = changed
            ? "已更新物体 " + name
            : "物体 " + name +
                " 未变化（需要 positionX/scaleX/rotationX 等字段）";
        return result;
    }

    EDITOR::EditorOperationResult
    DocumentSceneEditorController::ExecuteAiSceneObjectQuery(
        bool snapshot) {
        EDITOR::EditorOperationResult result{};
        DocumentSceneBase* scene = aiOperationScene_;
        if (scene == nullptr) {
            result.code = "NO_SCENE";
            result.message = "场景上下文不可用";
            return result;
        }
        result.ok = true;
        result.message = snapshot ? "场景快照：" : "场景对象：";
        SummarizeSceneObjects(*scene, result.details, 200u);
        if (result.details.empty()) {
            result.message = "场景为空";
        }
        return result;
    }

    EDITOR::EditorOperationResult
    DocumentSceneEditorController::ExecuteAiSceneQuery(
        const nlohmann::json& parameters) {
        EDITOR::EditorOperationResult result{};
        DocumentSceneBase* scene = aiOperationScene_;
        if (scene == nullptr) {
            result.code = "NO_SCENE";
            result.message = "场景上下文不可用";
            return result;
        }
        const std::string nameFilter =
            parameters.value("name", std::string{});
        result.ok = true;
        result.message = nameFilter.empty()
            ? "场景查询："
            : "场景查询（name 包含 " + nameFilter + "）：";
        size_t count = 0u;
        for (const auto& object : scene->GetWorld().GetObjects()) {
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

    EDITOR::EditorOperationResult
    DocumentSceneEditorController::ExecuteAiAssetListModels() {
        EDITOR::EditorOperationResult result{};
        DocumentSceneBase* scene = aiOperationScene_;
        if (scene == nullptr) {
            result.code = "NO_SCENE";
            result.message = "场景上下文不可用";
            return result;
        }
        result.ok = true;
        result.message = "模型资产：";
        for (const AssetDescriptor* descriptor :
             scene->GetAssetRegistry().CollectByType(
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

    EDITOR::EditorOperationResult
    DocumentSceneEditorController::ExecuteAiCapturePreview(
        const nlohmann::json& parameters) {
        EDITOR::EditorOperationResult result{};
        std::string path =
            parameters.value("path", std::string{});
        std::string captureError;
        if (CapturePreviewToFile(path, captureError)) {
            result.ok = true;
            result.message = "预览截图已保存：" + path;
        } else {
            result.code = "CAPTURE_FAILED";
            result.message = "截图失败：" + captureError;
        }
        return result;
    }

    EDITOR::EditorOperationResult
    DocumentSceneEditorController::ExecuteAiMaterialGraphQuery(
        const nlohmann::json& parameters) {
        EDITOR::EditorOperationResult result{};
        DocumentSceneBase* scene = aiOperationScene_;
        if (scene == nullptr) {
            result.code = "NO_SCENE";
            result.message = "场景上下文不可用";
            return result;
        }
        std::string message;
        std::string code;
        std::vector<std::string> details;
        if (modelMaterialWorkspaceController_.QueryAiMaterialGraph(
                *scene,
                parameters.value("name", std::string{}),
                message, details, code)) {
            result.ok = true;
            result.code = "OK";
            result.message = message;
            result.details = std::move(details);
        } else {
            result.code = code;
            result.message = message;
            result.details = std::move(details);
        }
        return result;
    }

    EDITOR::EditorOperationResult
    DocumentSceneEditorController::ExecuteAiMaterialGraphLayout() {
        EDITOR::EditorOperationResult result{};
        DocumentSceneBase* scene = aiOperationScene_;
        if (scene == nullptr) {
            result.code = "NO_SCENE";
            result.message = "场景上下文不可用";
            return result;
        }
        std::string message;
        std::string code;
        if (modelMaterialWorkspaceController_.LayoutAiMaterialGraph(
                *scene, message, code)) {
            result.ok = true;
            result.code = "OK";
            result.message = message;
            modelMaterialWorkspaceController_.AppendAiCompileDiagnostics(
                result.details);
        } else {
            result.code = code;
            result.message = message;
        }
        return result;
    }

    EDITOR::EditorOperationResult
    DocumentSceneEditorController::ExecuteAiAssetImport(
        const nlohmann::json& parameters) {
        EDITOR::EditorOperationResult result{};
        DocumentSceneBase* scene = aiOperationScene_;
        if (scene == nullptr) {
            result.code = "NO_SCENE";
            result.message = "场景上下文不可用";
            return result;
        }
        const std::string sourcePathText =
            parameters.value("path", std::string{});
        if (sourcePathText.empty()) {
            result.code = "MISSING_FIELD";
            result.message = "缺少 path 字段";
            return result;
        }
        const std::filesystem::path sourcePath(sourcePathText);
        std::error_code fileError;
        if (!std::filesystem::is_regular_file(
                sourcePath, fileError)) {
            result.code = "FILE_NOT_FOUND";
            result.message = "文件不存在或不可读：" + sourcePathText;
            return result;
        }
        if (!EDITOR::ASSET_BROWSER::IsSupportedImportSource(
                sourcePath)) {
            result.code = "UNSUPPORTED_TYPE";
            result.message = "不支持的文件类型：" +
                sourcePath.extension().string();
            return result;
        }
        AssetDatabase& assetDatabase = scene->GetAssetDatabase();
        const std::filesystem::path targetRelative =
            EDITOR::ASSET_BROWSER::SuggestedTargetDirectory(
                {}, sourcePath) /
            sourcePath.filename();
        std::filesystem::path copiedRelative;
        std::string copyError;
        if (!EDITOR::ASSET_BROWSER::CopySourceFileIntoProject(
                assetDatabase, sourcePath, targetRelative,
                false, copiedRelative, copyError)) {
            result.code = "COPY_FAILED";
            result.message = "导入复制失败：" + copyError;
            return result;
        }
        if (!assetDatabase.ScanAssets(true)) {
            result.code = "SCAN_FAILED";
            result.message = "资产扫描失败";
            return result;
        }
        const AssetRecord* record =
            assetDatabase.FindByPath(copiedRelative);
        if (record == nullptr) {
            result.code = "REGISTER_FAILED";
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

    EDITOR::EditorOperationResult
    DocumentSceneEditorController::ExecuteAiAssetImportStatus() {
        EDITOR::EditorOperationResult result{};
        DocumentSceneBase* scene = aiOperationScene_;
        if (scene == nullptr) {
            result.code = "NO_SCENE";
            result.message = "场景上下文不可用";
            return result;
        }
        const AssetImportBatchStatus status =
            scene->GetAssetDatabase().GetQueuedImportStatus();
        result.ok = true;
        result.message = status.active
            ? "导入进行中"
            : status.completed
                ? "导入已完成"
                : "没有活动导入";
        result.details = {
            "total: " + std::to_string(status.total),
            "finished: " + std::to_string(status.finished),
            "succeeded: " + std::to_string(status.succeeded),
            "failed: " + std::to_string(status.failed),
            "label: " + status.label,
        };
        if (!status.message.empty()) {
            result.details.push_back(
                "message: " + status.message);
        }
        return result;
    }

    EDITOR::EditorOperationResult
    DocumentSceneEditorController::ExecuteAiUndoLast() {
        EDITOR::EditorOperationResult result{};
        DocumentSceneBase* scene = aiOperationScene_;
        if (scene == nullptr) {
            result.code = "NO_SCENE";
            result.message = "场景上下文不可用";
            return result;
        }
        if (workspaceHost_.IsActive(
                EDITOR::EditorWorkspaceId::ModelMaterial)) {
            return operationRegistry_.Execute("material.undo");
        }
        if (workspaceHost_.IsActive(
                EDITOR::EditorWorkspaceId::ModelCollision)) {
            return operationRegistry_.Execute("collision.undo");
        }
        if (workspaceHost_.IsActive(
                EDITOR::EditorWorkspaceId::AnimationStateMachine)) {
            return operationRegistry_.Execute("asm.undo");
        }
        if (workspaceHost_.IsActive(
                EDITOR::EditorWorkspaceId::Cinematics) &&
            cinematicsWorkspaceController_.IsEditingSequenceAsset()) {
            return operationRegistry_.Execute("cinematics.undo");
        }
        if (!documentHistory_.CanUndo()) {
            result.code = "UNDO_EMPTY";
            result.message = "场景历史为空，没有可撤销的编辑";
            return result;
        }
        ExecuteSceneDocumentHistory(*scene, false);
        result.ok = true;
        result.code = "OK";
        result.message = "已撤销场景最近一次编辑";
        return result;
    }

    EDITOR::EditorOperationResult
    DocumentSceneEditorController::ExecuteAiSceneCreateObject(
        const nlohmann::json& parameters) {
        EDITOR::EditorOperationResult result{};
        DocumentSceneBase* scene = aiOperationScene_;
        if (scene == nullptr) {
            result.code = "NO_SCENE";
            result.message = "场景上下文不可用";
            return result;
        }
        const std::string model =
            parameters.value("model", std::string{});
        if (model.empty()) {
            result.code = "MISSING_FIELD";
            result.message =
                "缺少 model 字段（资产 id 或路径片段，"
                "可用 asset.list_models 查询）";
            return result;
        }
        const AssetRegistry& registry =
            scene->GetAssetRegistry();
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
            result.code = "MODEL_NOT_FOUND";
            result.message = "找不到模型资产：" + model;
            return result;
        }
        const bool dirtyBefore =
            context_.sceneDirty || scene->HasUnsavedSceneChanges();
        const std::vector<SceneObjectData> beforeObjects =
            scene->GetSceneDocument().objects;
        const SceneCameraSettings beforeCamera =
            scene->GetSceneDocument().camera;
        EDITOR::CreateObjectRequest request{};
        request.name =
            parameters.value("name", std::string{});
        if (request.name.empty()) {
            request.name = "AI Model";
        }
        request.position = {
            ReadJsonFloat(parameters, "positionX", 0.0f),
            ReadJsonFloat(parameters, "positionY", 0.0f),
            ReadJsonFloat(parameters, "positionZ", 0.0f),
        };
        request.scale = {
            ReadJsonFloat(parameters, "scaleX", 1.0f),
            ReadJsonFloat(parameters, "scaleY", 1.0f),
            ReadJsonFloat(parameters, "scaleZ", 1.0f),
        };
        GameObject* object = EDITOR::CreateModelObject(
            *scene,
            AssetGuid{ descriptor->id.value },
            request);
        if (object == nullptr) {
            result.code = "CREATE_FAILED";
            result.message = "创建模型物体失败：" + request.name;
            return result;
        }
        context_.selection.SelectObject(scene->GetWorld(), object);
        selectionSync_.SyncNextSceneObjectId(
            *scene, context_.nextSceneObjectId);
        CommitSceneObjectAuthoringHistory(
            *scene,
            SceneObjectAuthoringHistoryRequest{
                "Create Model Object",
                beforeObjects,
                scene->GetSceneDocument().objects,
                beforeCamera,
                scene->GetSceneDocument().camera,
                dirtyBefore });
        result.ok = true;
        SummarizeSceneObjects(
            *scene, result.details, 8u);
        result.message = "已创建模型 " + request.name +
            "（" + descriptor->sourcePath + "）";
        return result;
    }

    EDITOR::EditorOperationResult
    DocumentSceneEditorController::ExecuteAiProceduralCreatePrimitive(
        const nlohmann::json& parameters) {
        EDITOR::EditorOperationResult result{};
        DocumentSceneBase* scene = aiOperationScene_;
        if (scene == nullptr) {
            result.code = "NO_SCENE";
            result.message = "场景上下文不可用";
            return result;
        }
        const std::string kind =
            parameters.value("kind", std::string{});
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
            parameters.value("name", std::string{});
        if (request.object.name.empty()) {
            request.object.name =
                kind.empty() ? "AI Primitive" : kind;
        }
        request.object.position = {
            ReadJsonFloat(parameters, "positionX", 0.0f),
            ReadJsonFloat(parameters, "positionY", 0.0f),
            ReadJsonFloat(parameters, "positionZ", 0.0f),
        };
        request.object.scale = {
            ReadJsonFloat(parameters, "scaleX", 1.0f),
            ReadJsonFloat(parameters, "scaleY", 1.0f),
            ReadJsonFloat(parameters, "scaleZ", 1.0f),
        };
        request.mesh.kind = meshKind;
        request.mesh.width =
            ReadJsonFloat(parameters, "width", 1.0f);
        request.mesh.height =
            ReadJsonFloat(parameters, "height", 1.0f);
        request.mesh.depth =
            ReadJsonFloat(parameters, "depth", 1.0f);
        request.mesh =
            SanitizeProceduralMeshSettings(request.mesh);
        const bool dirtyBefore =
            context_.sceneDirty || scene->HasUnsavedSceneChanges();
        const std::vector<SceneObjectData> beforeObjects =
            scene->GetSceneDocument().objects;
        const SceneCameraSettings beforeCamera =
            scene->GetSceneDocument().camera;
        GameObject* object =
            EDITOR::CreatePrimitiveObject(*scene, request);
        if (object == nullptr) {
            result.code = "CREATE_FAILED";
            result.message =
                "创建程序化物体失败：" + request.object.name;
            return result;
        }
        context_.selection.SelectObject(scene->GetWorld(), object);
        selectionSync_.SyncNextSceneObjectId(
            *scene, context_.nextSceneObjectId);
        CommitSceneObjectAuthoringHistory(
            *scene,
            SceneObjectAuthoringHistoryRequest{
                "Create Primitive",
                beforeObjects,
                scene->GetSceneDocument().objects,
                beforeCamera,
                scene->GetSceneDocument().camera,
                dirtyBefore });
        char positionText[96];
        std::snprintf(
            positionText, sizeof(positionText),
            "(%.2f, %.2f, %.2f)",
            request.object.position.x,
            request.object.position.y,
            request.object.position.z);
        SummarizeSceneObjects(
            *scene, result.details, 8u);
        result.ok = true;
        result.message =
            "已创建 " + request.object.name + " " +
            std::string(positionText);
        return result;
    }

    EDITOR::EditorOperationResult
    DocumentSceneEditorController::ExecuteAiMaterialEditParameter(
        const nlohmann::json& parameters) {
        EDITOR::EditorOperationResult result{};
        DocumentSceneBase* scene = aiOperationScene_;
        if (scene == nullptr) {
            result.code = "NO_SCENE";
            result.message = "场景上下文不可用";
            return result;
        }
        const std::string parameter =
            parameters.value("parameter", std::string{});
        if (parameter.empty()) {
            result.code = "MISSING_FIELD";
            result.message = "缺少 parameter 字段";
            result.hints = {
                "请带上完整参数名，例如 "
                "{\"action\":\"material.edit_parameter\","
                "\"parameter\":\"parameter.baseColor\","
                "\"value\":0.8}",
            };
            return result;
        }
        float value = 0.0f;
        const auto valueIt = parameters.find("value");
        if (valueIt != parameters.end()) {
            if (valueIt->is_number()) {
                value = valueIt->get<float>();
            } else if (valueIt->is_string()) {
                const std::string valueText =
                    valueIt->get<std::string>();
                try {
                    value = std::stof(valueText);
                } catch (...) {
                    result.code = "INVALID_VALUE";
                    result.message =
                        "value 不是有效数字：" + valueText;
                    return result;
                }
            }
        }
        std::string message;
        std::string code;
        if (modelMaterialWorkspaceController_
                .ApplyAiMaterialParameterEdit(
                    *scene, parameter, value, message,
                    code)) {
            result.ok = true;
            result.code = "OK";
            result.message = message;
            modelMaterialWorkspaceController_
                .AppendAiCompileDiagnostics(
                    result.details);
        } else {
            result.code = code;
            result.message = message;
            AppendAiHints(code, result);
        }
        return result;
    }

    EDITOR::EditorOperationResult
    DocumentSceneEditorController::ExecuteAiMaterialGraphEdit(
        const nlohmann::json& parameters) {
        EDITOR::EditorOperationResult result{};
        DocumentSceneBase* scene = aiOperationScene_;
        if (scene == nullptr) {
            result.code = "NO_SCENE";
            result.message = "场景上下文不可用";
            return result;
        }
        EDITOR::AiMaterialGraphEditRequest request{};
        request.operation =
            parameters.value("operation", std::string{});
        if (request.operation.empty()) {
            request.operation =
                parameters.value("op", std::string{});
        }
        request.nodeType =
            parameters.value("nodeType", std::string{});
        request.name =
            parameters.value("name", std::string{});
        if (request.name.empty()) {
            request.name =
                parameters.value("node", std::string{});
        }
        request.sourceNode =
            parameters.value("sourceNode", std::string{});
        request.sourcePin =
            parameters.value("sourcePin", std::string{});
        request.targetNode =
            parameters.value("targetNode", std::string{});
        request.targetPin =
            parameters.value("targetPin", std::string{});
        request.edgeId =
            parameters.value("edgeId", std::string{});
        request.valueType =
            parameters.value("valueType", std::string{});
        request.property =
            parameters.value("property", std::string{});
        request.resource =
            parameters.value("resource", std::string{});
        request.stringValue =
            parameters.value("value", std::string{});
        request.manualPosition =
            parameters.value("layout", std::string{}) ==
            "manual";
        request.positionX =
            ReadJsonFloat(parameters, "positionX", 0.0f);
        request.positionY =
            ReadJsonFloat(parameters, "positionY", 0.0f);
        const auto hasNumberField =
            [&parameters](const char* key) {
                const auto found = parameters.find(key);
                return found != parameters.end() &&
                    (found->is_number() ||
                     (found->is_string() &&
                      !found->get<std::string>().empty()));
            };
        request.valueSet =
            hasNumberField("valueX") ||
            hasNumberField("valueY") ||
            hasNumberField("valueZ") ||
            hasNumberField("valueW");
        request.valueX =
            ReadJsonFloat(parameters, "valueX", 0.0f);
        request.valueY =
            ReadJsonFloat(parameters, "valueY", 0.0f);
        request.valueZ =
            ReadJsonFloat(parameters, "valueZ", 0.0f);
        request.valueW =
            ReadJsonFloat(parameters, "valueW", 0.0f);
        std::string message;
        std::string code;
        std::vector<std::string> graphDetails;
        if (modelMaterialWorkspaceController_
                .ApplyAiMaterialGraphEdit(
                    *scene, request, message,
                    graphDetails, code)) {
            result.ok = true;
            result.code = "OK";
            result.message = message;
            result.details = std::move(graphDetails);
            modelMaterialWorkspaceController_
                .AppendAiCompileDiagnostics(
                    result.details);
        } else {
            result.code = code;
            result.message = message;
            result.details = std::move(graphDetails);
            AppendAiHints(code, result);
        }
        return result;
    }

    EDITOR::EditorOperationResult
    DocumentSceneEditorController::ExecuteAiMaterialVerify(
        const nlohmann::json& parameters) {
        EDITOR::EditorOperationResult result{};
        std::string capturePath;
        std::string captureError;
        if (!CapturePreviewToFile(
                capturePath, captureError)) {
            result.code = "CAPTURE_FAILED";
            result.message =
                "截图失败：" + captureError;
            return result;
        }
        EDITOR::AI_ASSISTANT::AiChatConfig vision{};
        if (!EDITOR::AI_ASSISTANT::
                LoadAiVisionConfig(vision)) {
            result.code = "VISION_NOT_CONFIGURED";
            result.message =
                "截图已保存：" + capturePath +
                "，但未配置视觉 API";
            result.details = { capturePath };
            return result;
        }
        const std::string prompt =
            parameters.value("prompt", std::string{});
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
            result.code = "VISION_FAILED";
            result.message =
                "截图已保存：" + capturePath +
                "，识图失败：" + error;
            result.details = { capturePath };
            return result;
        }
        result.ok = true;
        result.code = "OK";
        result.message = "预览验证：" + text;
        result.details = { "截图：" + capturePath };
        return result;
    }

    EDITOR::EditorOperationResult
    DocumentSceneEditorController::ExecuteSceneSave() {
        EDITOR::EditorOperationResult result{};
        DocumentSceneBase* scene = aiOperationScene_;
        if (scene == nullptr) {
            result.code = "NO_SCENE";
            result.message = "场景上下文不可用";
            return result;
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

    EDITOR::EditorOperationResult
    DocumentSceneEditorController::ExecuteSceneDeleteObject(
        const nlohmann::json& parameters) {
        EDITOR::EditorOperationResult result{};
        DocumentSceneBase* scene = aiOperationScene_;
        if (scene == nullptr) {
            result.code = "NO_SCENE";
            result.message = "场景上下文不可用";
            return result;
        }
        const std::string name =
            parameters.value("name", std::string{});
        if (!name.empty()) {
            GameObject* target = nullptr;
            for (const auto& object :
                 scene->GetWorld().GetObjects()) {
                if (object->GetName() == name) {
                    target = object.get();
                    break;
                }
            }
            if (target == nullptr) {
                result.code = "OBJECT_NOT_FOUND";
                result.message = "找不到物体：" + name;
                return result;
            }
            context_.selection.SelectObject(
                scene->GetWorld(),
                target);
        }
        if (!sceneObjectCommands_.CanExecute(
                EDITOR::SceneObjectCommandId::Delete,
                *scene,
                context_)) {
            result.code = "INVALID_STATE";
            result.message = "没有可删除的选中对象";
            return result;
        }
        if (!sceneObjectCommands_.Execute(
                EDITOR::SceneObjectCommandId::Delete,
                *scene,
                context_,
                selectionSync_)) {
            result.code = "DELETE_FAILED";
            result.message = "删除失败";
            return result;
        }
        if (std::optional<SceneObjectAuthoringHistoryRequest> history =
                sceneObjectCommands_.ConsumeHistoryRequest()) {
            CommitSceneObjectAuthoringHistory(
                *scene, std::move(*history));
        }
        result.ok = true;
        result.code = "OK";
        result.message = name.empty()
            ? "已删除选中对象"
            : "已删除对象 " + name;
        return result;
    }

    EDITOR::EditorOperationResult
    DocumentSceneEditorController::ExecuteSceneDuplicateObject(
        const nlohmann::json& parameters) {
        EDITOR::EditorOperationResult result{};
        DocumentSceneBase* scene = aiOperationScene_;
        if (scene == nullptr) {
            result.code = "NO_SCENE";
            result.message = "场景上下文不可用";
            return result;
        }
        const std::string name =
            parameters.value("name", std::string{});
        if (!name.empty()) {
            GameObject* target = nullptr;
            for (const auto& object :
                 scene->GetWorld().GetObjects()) {
                if (object->GetName() == name) {
                    target = object.get();
                    break;
                }
            }
            if (target == nullptr) {
                result.code = "OBJECT_NOT_FOUND";
                result.message = "找不到物体：" + name;
                return result;
            }
            context_.selection.SelectObject(
                scene->GetWorld(),
                target);
        }
        if (!sceneObjectCommands_.CanExecute(
                EDITOR::SceneObjectCommandId::Duplicate,
                *scene,
                context_)) {
            result.code = "INVALID_STATE";
            result.message = "没有可复制的选中对象";
            return result;
        }
        if (!sceneObjectCommands_.Execute(
                EDITOR::SceneObjectCommandId::Duplicate,
                *scene,
                context_,
                selectionSync_)) {
            result.code = "DUPLICATE_FAILED";
            result.message = "复制失败";
            return result;
        }
        if (std::optional<SceneObjectAuthoringHistoryRequest> history =
                sceneObjectCommands_.ConsumeHistoryRequest()) {
            CommitSceneObjectAuthoringHistory(
                *scene, std::move(*history));
        }
        result.ok = true;
        result.code = "OK";
        result.message = name.empty()
            ? "已复制选中对象"
            : "已复制对象 " + name;
        return result;
    }

    EDITOR::EditorOperationResult
    DocumentSceneEditorController::ExecuteSceneCreateEmptyObject(
        const nlohmann::json& parameters) {
        EDITOR::EditorOperationResult result{};
        DocumentSceneBase* scene = aiOperationScene_;
        if (scene == nullptr) {
            result.code = "NO_SCENE";
            result.message = "场景上下文不可用";
            return result;
        }
        const bool dirtyBefore =
            context_.sceneDirty || scene->HasUnsavedSceneChanges();
        const std::vector<SceneObjectData> beforeObjects =
            scene->GetSceneDocument().objects;
        const SceneCameraSettings beforeCamera =
            scene->GetSceneDocument().camera;
        EDITOR::CreateObjectRequest request{};
        request.name =
            parameters.value("name", std::string{});
        if (request.name.empty()) {
            request.name = "AI Object";
        }
        request.position = {
            ReadJsonFloat(parameters, "positionX", 0.0f),
            ReadJsonFloat(parameters, "positionY", 0.0f),
            ReadJsonFloat(parameters, "positionZ", 0.0f),
        };
        request.scale = {
            ReadJsonFloat(parameters, "scaleX", 1.0f),
            ReadJsonFloat(parameters, "scaleY", 1.0f),
            ReadJsonFloat(parameters, "scaleZ", 1.0f),
        };
        GameObject* object =
            EDITOR::CreateEmptyObject(*scene, request);
        if (object == nullptr) {
            result.code = "CREATE_FAILED";
            result.message = "创建空物体失败：" + request.name;
            return result;
        }
        context_.selection.SelectObject(scene->GetWorld(), object);
        selectionSync_.SyncNextSceneObjectId(
            *scene, context_.nextSceneObjectId);
        CommitSceneObjectAuthoringHistory(
            *scene,
            SceneObjectAuthoringHistoryRequest{
                "Create Empty Object",
                beforeObjects,
                scene->GetSceneDocument().objects,
                beforeCamera,
                scene->GetSceneDocument().camera,
                dirtyBefore });
        SummarizeSceneObjects(
            *scene, result.details, 8u);
        result.ok = true;
        result.message = "已创建空物体 " + request.name;
        return result;
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
        aiOperationScene_ = &scene;
        aiAssistantPanel_.PumpActions();
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
            CommitSceneObjectAuthoringHistory(
                scene, std::move(*history));
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
        if (context_.windows.runtime.showDebugView) {
            DrawDebugViewWindow(scene, context_.windows.runtime.showDebugView);
        }
        if (context_.windows.runtime.showPerformanceAudit) {
            performanceAuditPanel_.Draw(context_.windows.runtime.showPerformanceAudit);
        }
        {
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
            aiAssistantPanel_.SetContextSnapshot(std::move(context));
        }
        aiAssistantPanel_.SetSceneActionCallback(
            [this](std::string_view json) {
                const std::string action =
                    EDITOR::AI_ASSISTANT::ExtractJsonField(json, "action");
                if (operationRegistry_.IsRegistered(action)) {
                    try {
                        return ToAiRequestResult(
                            operationRegistry_.Execute(
                                action,
                                nlohmann::json::parse(
                                    std::string(json))));
                    } catch (...) {
                        EDITOR::AI_ASSISTANT::AiRequestResult result{};
                        result.code = "INVALID_JSON";
                        result.message = "动作 JSON 解析失败";
                        return result;
                    }
                }
                EDITOR::AI_ASSISTANT::AiRequestResult result{};
                result.ok = false;
                result.code = "UNKNOWN_ACTION";
                result.message = "不支持的 AI 动作：" + action;
                for (const std::string& id :
                     operationRegistry_.RegisteredIds()) {
                    result.details.push_back("supported: " + id);
                }
                return result;
            });
        EnsureAiLocalBridge();
        if (context_.windows.runtime.showAiAssistant) {
            aiAssistantPanel_.Draw(context_.windows.runtime.showAiAssistant);
        }
        if (context_.windows.runtime.showEditorSettings) {
            editorSettingsPanel_.Draw(
                context_.windows.runtime.showEditorSettings,
                context_);
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
