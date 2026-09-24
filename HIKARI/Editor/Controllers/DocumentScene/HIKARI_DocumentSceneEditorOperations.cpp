#include "Editor/Preview/HIKARI_EditorPreviewCapture.h"
#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include "Editor/Authoring/HIKARI_ComponentAuthoringOperations.h"
#include "Editor/Workspaces/AnimationStateMachine/HIKARI_AnimationStateMachineOperations.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsOperations.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionOperations.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialOperations.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialNodeQuery.h"
#include "Editor/Workspaces/RuntimeBinding/HIKARI_RuntimeBindingOperations.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxOperations.h"
#include "Editor/Workspaces/Dialogue/HIKARI_DialogueOperations.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Runtime/Diagnostics/HIKARI_RuntimeDebugService.h"
#include "Scene/HIKARI_World.h"

#include <utility>

namespace HIKARI {

void DocumentSceneEditorController::RegisterEditorOperations() {
    RegisterUiOperations();
    RegisterSceneFolderOperations();
    RegisterSceneLayoutOperations();
    EDITOR::RegisterProjectCodeOperations(operationRegistry_, projectCodeJob_, [this] {
        return projectBuildAllowed_ && aiOperationScene_ && !aiOperationScene_->IsRuntimePlayActive();
    });
    EDITOR::RegisterGameExportOperations(operationRegistry_, exportJob_, [this] {
        return projectBuildAllowed_ && aiOperationScene_ && !HasKnownUnsavedProjectChanges(*aiOperationScene_);
    });
    using EDITOR::EditorOperationParameterType;
    operationRegistry_.Register(
        {"runtime.debug.execute",
         "Execute a bounded developer command against the playing World. Same owner as the F10 console; use help to discover commands.",
         "runtime",
         {{"command", EditorOperationParameterType::String, true, "Developer command, up to 512 bytes."}},
         [this](const nlohmann::json& parameters) -> EDITOR::EditorOperationResult {
             if (!aiOperationScene_ || !aiOperationScene_->IsRuntimePlayActive())
                 return {false, "PLAY_REQUIRED", "Start Play first."};
             auto& world = aiOperationScene_->GetWorld();
             auto* debug = world.Services().Find<RUNTIME::RuntimeDebugService>();
             if (!debug)
                 return {false, "UNAVAILABLE", "Developer diagnostics are unavailable."};
             const auto reply = debug->Execute(world, parameters.value("command", std::string{}));
             return {reply.success, reply.success ? "OK" : "REJECTED", reply.text};
         }},
        HIKARI::EDITOR::EditorOperationKind::Session, false);

    operationRegistry_.Register(
        {
            "material.node_info",
            "Return one node's semantic contract.",
            "model_material",
            {
                EDITOR::EditorOperationParameter{"nodeType", EditorOperationParameterType::String, false,
                                                 "稳定节点类型名，例如 node.additive"},
                EDITOR::EditorOperationParameter{"name", EditorOperationParameterType::String, false,
                                                 "节点类型别名，与 nodeType 二选一"},
            },
            [](const nlohmann::json& parameters) -> EDITOR::EditorOperationResult {
                std::string nodeType = parameters.value("nodeType", std::string{});
                if (nodeType.empty()) {
                    nodeType = parameters.value("name", std::string{});
                }
                EDITOR::EditorOperationResult result{};
                std::string message;
                std::string code;
                std::vector<std::string> details;
                if (EDITOR::DescribeAiMaterialNode(nodeType, message, details, code)) {
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
        },
        HIKARI::EDITOR::EditorOperationKind::Query, false);
    operationRegistry_.Register(
        {
            "scene.set_transform",
            "Set one object's transform by stable objectId. Exact name is a discovery fallback and must be unique.",
            "scene",
            {
                EDITOR::EditorOperationParameter{"objectId", EditorOperationParameterType::Int, false,
                                                 "scene.objects.query 返回的稳定 SceneObjectId"},
                EDITOR::EditorOperationParameter{"name", EditorOperationParameterType::String, false,
                                                 "仅兼容发现；重名时必须改用 objectId"},
                EDITOR::EditorOperationParameter{"positionX", EditorOperationParameterType::Float, false, "世界位置 X"},
                EDITOR::EditorOperationParameter{"positionY", EditorOperationParameterType::Float, false, "世界位置 Y"},
                EDITOR::EditorOperationParameter{"positionZ", EditorOperationParameterType::Float, false, "世界位置 Z"},
                EDITOR::EditorOperationParameter{"scaleX", EditorOperationParameterType::Float, false, "缩放 X"},
                EDITOR::EditorOperationParameter{"scaleY", EditorOperationParameterType::Float, false, "缩放 Y"},
                EDITOR::EditorOperationParameter{"scaleZ", EditorOperationParameterType::Float, false, "缩放 Z"},
                EDITOR::EditorOperationParameter{"rotationX", EditorOperationParameterType::Float, false,
                                                 "欧拉旋转 X（度）"},
                EDITOR::EditorOperationParameter{"rotationY", EditorOperationParameterType::Float, false,
                                                 "欧拉旋转 Y（度）"},
                EDITOR::EditorOperationParameter{"rotationZ", EditorOperationParameterType::Float, false,
                                                 "欧拉旋转 Z（度）"},
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteSceneObjectOperation(EDITOR::SetSceneObjectTransform, parameters);
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);
    operationRegistry_.Register(
        {
            "scene.list_objects",
            "List scene objects with position and scale.",
            "scene",
            {},
            [this](const nlohmann::json&) { return ExecuteSceneObjectOperation(EDITOR::QuerySceneObjects, {}); },
        },
        HIKARI::EDITOR::EditorOperationKind::Query);
    operationRegistry_.Register(
        {
            "scene.snapshot",
            "Return a compact scene object snapshot.",
            "scene",
            {},
            [this](const nlohmann::json&) { return ExecuteSceneObjectOperation(EDITOR::QuerySceneObjects, {}); },
        },
        HIKARI::EDITOR::EditorOperationKind::Query);
    operationRegistry_.Register(
        {
            "scene.query",
            "Query scene objects by name filter.",
            "scene",
            {
                EDITOR::EditorOperationParameter{"name", EditorOperationParameterType::String, false, "按名称包含过滤"},
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteSceneObjectOperation(EDITOR::QuerySceneObjects, parameters);
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Query);
    operationRegistry_.Register(
        {
            "asset.list_models",
            "List registered model assets.",
            "assets",
            {},
            [this](const nlohmann::json&) { return ExecuteAiAssetListModels(); },
        },
        HIKARI::EDITOR::EditorOperationKind::Query, false);
    operationRegistry_.Register(
        {"capture.status",
         "Poll a capture ticket and return its actual image.",
         "general",
         {EDITOR::EditorOperationParameter{"captureId", EditorOperationParameterType::Int, true, "Capture ticket"}},
         [this](const nlohmann::json& p) { return PollAiPreviewCapture(p.at("captureId").get<uint64_t>()); }},
        HIKARI::EDITOR::EditorOperationKind::Query, false);
    operationRegistry_.Register(
        {
            "capture.preview",
            "Capture the current visible preview as a real image. Poll capture.status until complete.",
            "general",
            {
                EDITOR::EditorOperationParameter{"path", EditorOperationParameterType::String, false,
                                                 "输出文件路径，缺省为临时目录"},
            },
            [this](const nlohmann::json& parameters) { return ExecuteAiCapturePreview(parameters); },
        },
        HIKARI::EDITOR::EditorOperationKind::Session, true, HIKARI::EDITOR::EditorOperationSessionDispatch::Preview);

    operationRegistry_.Register(
        {
            "asset.import",
            "Copy a supported source file into the project and queue its import.",
            "assets",
            {
                EDITOR::EditorOperationParameter{"path", EditorOperationParameterType::String, true, "源文件绝对路径"},
            },
            [this](const nlohmann::json& parameters) { return ExecuteAiAssetImport(parameters); },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation, false);
    operationRegistry_.Register(
        {
            "asset.import_status",
            "Report the current queued asset import progress.",
            "assets",
            {},
            [this](const nlohmann::json&) { return ExecuteAiAssetImportStatus(); },
        },
        HIKARI::EDITOR::EditorOperationKind::Query, false);
    operationRegistry_.Register(
        {
            "action.undo_last",
            "Undo the most recent edit in the active workspace.",
            "general",
            {},
            [this](const nlohmann::json&) { return ExecuteAiUndoLast(); },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);
    operationRegistry_.Register(
        {
            "material.node_list",
            "List discoverable material graph node types.",
            "model_material",
            {
                EDITOR::EditorOperationParameter{"keyword", EditorOperationParameterType::String, false,
                                                 "按关键字过滤节点类型"},
                EDITOR::EditorOperationParameter{"class", EditorOperationParameterType::String, false,
                                                 "按节点类别过滤"},
            },
            [](const nlohmann::json& parameters) {
                EDITOR::EditorOperationResult result{};
                std::string message;
                std::string code;
                std::vector<std::string> details;
                if (EDITOR::ListAiMaterialNodes(parameters.value("keyword", std::string{}),
                                                parameters.value("class", std::string{}), message, details, code)) {
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
        },
        HIKARI::EDITOR::EditorOperationKind::Query, false);
    operationRegistry_.Register(
        {
            "scene.create_object",
            "Create a model object in the scene by asset id or path fragment.",
            "scene",
            {
                EDITOR::EditorOperationParameter{"model", EditorOperationParameterType::String, true,
                                                 "模型资产 id 或路径片段"},
                EDITOR::EditorOperationParameter{"name", EditorOperationParameterType::String, false,
                                                 "物体名称，缺省为 AI Model"},
                EDITOR::EditorOperationParameter{"positionX", EditorOperationParameterType::Float, false, "世界位置 X"},
                EDITOR::EditorOperationParameter{"positionY", EditorOperationParameterType::Float, false, "世界位置 Y"},
                EDITOR::EditorOperationParameter{"positionZ", EditorOperationParameterType::Float, false, "世界位置 Z"},
                EDITOR::EditorOperationParameter{"scaleX", EditorOperationParameterType::Float, false, "缩放 X"},
                EDITOR::EditorOperationParameter{"scaleY", EditorOperationParameterType::Float, false, "缩放 Y"},
                EDITOR::EditorOperationParameter{"scaleZ", EditorOperationParameterType::Float, false, "缩放 Z"},
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteSceneObjectOperation(EDITOR::CreateSceneModel, parameters);
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);
    operationRegistry_.Register(
        {
            "scene.create_world_sprite",
            "Create a 3D world sprite directly from a texture asset id or path fragment.",
            "scene",
            {
                EDITOR::EditorOperationParameter{"texture", EditorOperationParameterType::String, true,
                                                 "纹理资产 id 或路径片段"},
                EDITOR::EditorOperationParameter{"name", EditorOperationParameterType::String, false,
                                                 "物体名称，缺省使用纹理名"},
                EDITOR::EditorOperationParameter{"positionX", EditorOperationParameterType::Float, false, "世界位置 X"},
                EDITOR::EditorOperationParameter{"positionY", EditorOperationParameterType::Float, false, "世界位置 Y"},
                EDITOR::EditorOperationParameter{"positionZ", EditorOperationParameterType::Float, false, "世界位置 Z"},
                EDITOR::EditorOperationParameter{"scaleX", EditorOperationParameterType::Float, false, "缩放 X"},
                EDITOR::EditorOperationParameter{"scaleY", EditorOperationParameterType::Float, false, "缩放 Y"},
                EDITOR::EditorOperationParameter{"scaleZ", EditorOperationParameterType::Float, false, "缩放 Z"},
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteSceneObjectOperation(EDITOR::CreateSceneWorldSprite, parameters);
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);
    operationRegistry_.Register(
        {
            "scene.create_world_text",
            "Create editable SDF text directly in 3D world space.",
            "scene",
            {
                EDITOR::EditorOperationParameter{"text", EditorOperationParameterType::String, true, "UTF-8 文本内容"},
                EDITOR::EditorOperationParameter{"fontAssetPath", EditorOperationParameterType::String, false,
                                                 "项目 Fonts 或 Assets 下的相对字体路径"},
                EDITOR::EditorOperationParameter{"name", EditorOperationParameterType::String, false,
                                                 "物体名称，缺省为 World Text"},
                EDITOR::EditorOperationParameter{"positionX", EditorOperationParameterType::Float, false, "世界位置 X"},
                EDITOR::EditorOperationParameter{"positionY", EditorOperationParameterType::Float, false, "世界位置 Y"},
                EDITOR::EditorOperationParameter{"positionZ", EditorOperationParameterType::Float, false, "世界位置 Z"},
                EDITOR::EditorOperationParameter{"rotationX", EditorOperationParameterType::Float, false,
                                                 "欧拉旋转 X（度）"},
                EDITOR::EditorOperationParameter{"rotationY", EditorOperationParameterType::Float, false,
                                                 "欧拉旋转 Y（度）"},
                EDITOR::EditorOperationParameter{"rotationZ", EditorOperationParameterType::Float, false,
                                                 "欧拉旋转 Z（度）"},
                EDITOR::EditorOperationParameter{"scaleX", EditorOperationParameterType::Float, false, "缩放 X"},
                EDITOR::EditorOperationParameter{"scaleY", EditorOperationParameterType::Float, false, "缩放 Y"},
                EDITOR::EditorOperationParameter{"scaleZ", EditorOperationParameterType::Float, false, "缩放 Z"},
                EDITOR::EditorOperationParameter{"layoutWidth", EditorOperationParameterType::Float, false,
                                                 "文本布局宽度（世界单位）"},
                EDITOR::EditorOperationParameter{"layoutHeight", EditorOperationParameterType::Float, false,
                                                 "文本布局高度（世界单位）"},
                EDITOR::EditorOperationParameter{"fontHeight", EditorOperationParameterType::Float, false,
                                                 "字高（世界单位）"},
                EDITOR::EditorOperationParameter{"colorR", EditorOperationParameterType::Float, false,
                                                 "文字线性色彩 R"},
                EDITOR::EditorOperationParameter{"colorG", EditorOperationParameterType::Float, false,
                                                 "文字线性色彩 G"},
                EDITOR::EditorOperationParameter{"colorB", EditorOperationParameterType::Float, false,
                                                 "文字线性色彩 B"},
                EDITOR::EditorOperationParameter{"outlineR", EditorOperationParameterType::Float, false,
                                                 "描边线性色彩 R"},
                EDITOR::EditorOperationParameter{"outlineG", EditorOperationParameterType::Float, false,
                                                 "描边线性色彩 G"},
                EDITOR::EditorOperationParameter{"outlineB", EditorOperationParameterType::Float, false,
                                                 "描边线性色彩 B"},
                EDITOR::EditorOperationParameter{"outlineWidth", EditorOperationParameterType::Float, false,
                                                 "SDF 描边宽度，范围 0-0.25"},
                EDITOR::EditorOperationParameter{"opacity", EditorOperationParameterType::Float, false,
                                                 "不透明度，范围 0-1"},
                EDITOR::EditorOperationParameter{"horizontalAlign", EditorOperationParameterType::String, false,
                                                 "left/center/right"},
                EDITOR::EditorOperationParameter{"verticalAlign", EditorOperationParameterType::String, false,
                                                 "top/center/bottom"},
                EDITOR::EditorOperationParameter{"wrap", EditorOperationParameterType::String, false, "none/character"},
                EDITOR::EditorOperationParameter{"facing", EditorOperationParameterType::String, false,
                                                 "fixed/camera_facing/fixed_y_axis"},
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteSceneObjectOperation(EDITOR::CreateSceneWorldText, parameters);
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);
    operationRegistry_.Register(
        {
            "procedural.create_primitive",
            "Create a procedural mesh object (box/sphere/plane/grid_plane/cylinder/capsule).",
            "scene",
            {
                EDITOR::EditorOperationParameter{"kind", EditorOperationParameterType::String, false,
                                                 "box/sphere/plane/grid_plane/cylinder/capsule"},
                EDITOR::EditorOperationParameter{"name", EditorOperationParameterType::String, false,
                                                 "物体名称，缺省使用 kind"},
                EDITOR::EditorOperationParameter{"positionX", EditorOperationParameterType::Float, false, "世界位置 X"},
                EDITOR::EditorOperationParameter{"positionY", EditorOperationParameterType::Float, false, "世界位置 Y"},
                EDITOR::EditorOperationParameter{"positionZ", EditorOperationParameterType::Float, false, "世界位置 Z"},
                EDITOR::EditorOperationParameter{"scaleX", EditorOperationParameterType::Float, false, "缩放 X"},
                EDITOR::EditorOperationParameter{"scaleY", EditorOperationParameterType::Float, false, "缩放 Y"},
                EDITOR::EditorOperationParameter{"scaleZ", EditorOperationParameterType::Float, false, "缩放 Z"},
                EDITOR::EditorOperationParameter{"width", EditorOperationParameterType::Float, false, "网格宽度"},
                EDITOR::EditorOperationParameter{"height", EditorOperationParameterType::Float, false, "网格高度"},
                EDITOR::EditorOperationParameter{"depth", EditorOperationParameterType::Float, false, "网格深度"},
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteSceneObjectOperation(EDITOR::CreateScenePrimitive, parameters);
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    operationRegistry_.Register(
        {
            "material.verify",
            "Capture the material preview as an image for the main model to evaluate.",
            "model_material",
            {
                EDITOR::EditorOperationParameter{"prompt", EditorOperationParameterType::String, false,
                                                 "识图提示词，缺省使用默认描述"},
            },
            [this](const nlohmann::json& parameters) { return ExecuteAiMaterialVerify(parameters); },
        },
        HIKARI::EDITOR::EditorOperationKind::Session, true, HIKARI::EDITOR::EditorOperationSessionDispatch::Preview);
    operationRegistry_.Register(
        {
            "scene.save",
            "Save the current scene document.",
            "scene",
            {},
            [this](const nlohmann::json&) { return ExecuteSceneSave(); },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);
    operationRegistry_.Register(
        {
            "scene.delete_object",
            "Delete the selected object, or target one object by stable objectId.",
            "scene",
            {
                EDITOR::EditorOperationParameter{"objectId", EditorOperationParameterType::Int, false,
                                                 "scene.objects.query 返回的稳定 SceneObjectId"},
                EDITOR::EditorOperationParameter{"name", EditorOperationParameterType::String, false,
                                                 "仅兼容发现；重名时必须改用 objectId"},
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteSceneObjectOperation(EDITOR::DeleteSceneObjects, parameters);
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);
    operationRegistry_.Register(
        {
            "scene.duplicate_object",
            "Duplicate the selected object, or target one object by stable objectId.",
            "scene",
            {
                EDITOR::EditorOperationParameter{"objectId", EditorOperationParameterType::Int, false,
                                                 "scene.objects.query 返回的稳定 SceneObjectId"},
                EDITOR::EditorOperationParameter{"name", EditorOperationParameterType::String, false,
                                                 "仅兼容发现；重名时必须改用 objectId"},
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteSceneObjectOperation(EDITOR::DuplicateSceneObjects, parameters);
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);
    operationRegistry_.Register(
        {
            "scene.create_empty_object",
            "Create an empty scene object.",
            "scene",
            {
                EDITOR::EditorOperationParameter{"name", EditorOperationParameterType::String, false,
                                                 "物体名称，缺省为 AI Object"},
                EDITOR::EditorOperationParameter{"positionX", EditorOperationParameterType::Float, false, "世界位置 X"},
                EDITOR::EditorOperationParameter{"positionY", EditorOperationParameterType::Float, false, "世界位置 Y"},
                EDITOR::EditorOperationParameter{"positionZ", EditorOperationParameterType::Float, false, "世界位置 Z"},
                EDITOR::EditorOperationParameter{"scaleX", EditorOperationParameterType::Float, false, "缩放 X"},
                EDITOR::EditorOperationParameter{"scaleY", EditorOperationParameterType::Float, false, "缩放 Y"},
                EDITOR::EditorOperationParameter{"scaleZ", EditorOperationParameterType::Float, false, "缩放 Z"},
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteSceneObjectOperation(EDITOR::CreateSceneEmptyObject, parameters);
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);
    operationRegistry_.Register(
        {
            "terrain.fill_layer",
            "Fill an entire terrain object with one material layer while preserving authored height and coverage.",
            "terrain_surface",
            {
                EDITOR::EditorOperationParameter{"objectId", EditorOperationParameterType::Int, false,
                                                 "scene.objects.query 返回的稳定 SceneObjectId"},
                EDITOR::EditorOperationParameter{"name", EditorOperationParameterType::String, false,
                                                 "仅兼容发现；重名时必须改用 objectId"},
                EDITOR::EditorOperationParameter{"presetId", EditorOperationParameterType::String, true,
                                                 "要填充的地形材质层 preset id"},
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteTerrainOperation(EDITOR::FillTerrainLayer, parameters);
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);
    operationRegistry_.Register(
        {
            "terrain.generate_procedural",
            "Generate deterministic hills, islands, ridges, basins, or dunes into a TerrainSurface range.",
            "terrain_surface",
            {
                EDITOR::EditorOperationParameter{"objectId", EditorOperationParameterType::Int, false,
                                                 "scene.objects.query 返回的稳定 SceneObjectId"},
                EDITOR::EditorOperationParameter{"name", EditorOperationParameterType::String, false,
                                                 "仅兼容发现；重名时必须改用 objectId"},
                EDITOR::EditorOperationParameter{"mode", EditorOperationParameterType::String, true,
                                                 "noise_hills/island_coast/ridge/basin_crater/dunes"},
                EDITOR::EditorOperationParameter{"blendMode", EditorOperationParameterType::String, false,
                                                 "add/replace/max，默认 add"},
                EDITOR::EditorOperationParameter{"seed", EditorOperationParameterType::Int, false, "确定性生成种子"},
                EDITOR::EditorOperationParameter{"centerX", EditorOperationParameterType::Float, false,
                                                 "地形本地 X 中心"},
                EDITOR::EditorOperationParameter{"centerZ", EditorOperationParameterType::Float, false,
                                                 "地形本地 Z 中心"},
                EDITOR::EditorOperationParameter{"extentX", EditorOperationParameterType::Float, false,
                                                 "本地 X 半范围"},
                EDITOR::EditorOperationParameter{"extentZ", EditorOperationParameterType::Float, false,
                                                 "本地 Z 半范围"},
                EDITOR::EditorOperationParameter{"amplitude", EditorOperationParameterType::Float, false, "高度振幅"},
                EDITOR::EditorOperationParameter{"baseHeight", EditorOperationParameterType::Float, false,
                                                 "替换模式基准高度"},
                EDITOR::EditorOperationParameter{"scale", EditorOperationParameterType::Float, false, "特征尺度"},
                EDITOR::EditorOperationParameter{"edgeFalloff", EditorOperationParameterType::Float, false, "边缘衰减"},
                EDITOR::EditorOperationParameter{"blendAmount", EditorOperationParameterType::Float, false, "混合量"},
                EDITOR::EditorOperationParameter{"directionDegrees", EditorOperationParameterType::Float, false,
                                                 "脊线/沙丘方向"},
                EDITOR::EditorOperationParameter{"octaves", EditorOperationParameterType::Int, false, "噪声层数"},
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteTerrainOperation(EDITOR::GenerateTerrain, parameters);
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);
    EDITOR::RegisterModelCollisionOperations(operationRegistry_, ModelCollisionWorkspace(),
                                             [this]() { return aiOperationScene_; });
    EDITOR::RegisterAnimationStateMachineOperations(operationRegistry_, AnimationStateMachineWorkspace(),
                                                    [this]() { return aiOperationScene_; });
    EDITOR::RegisterGameplayCameraOperations(operationRegistry_, GameplayCameraWorkspace(), workspaceHost_,
                                             [this] { return aiOperationScene_; });
    operationRegistry_.Register(
        {"gameplay_camera.bind",
         "Bind a saved camera profile to scene targets, creating a camera when cameraId is zero. Includes scene undo and sets the default gameplay camera.",
         "scene",
         {EDITOR::EditorOperationParameter{"assetGuid", EditorOperationParameterType::String, true,
                                           "Gameplay camera asset GUID"},
          EDITOR::EditorOperationParameter{"cameraId", EditorOperationParameterType::Int, false,
                                           "Existing camera or zero to create"},
          EDITOR::EditorOperationParameter{"targetId", EditorOperationParameterType::Int, true,
                                           "Follow target scene object ID"},
          EDITOR::EditorOperationParameter{"lookTargetId", EditorOperationParameterType::Int, false,
                                           "Optional lock target ID"},
          EDITOR::EditorOperationParameter{"mode", EditorOperationParameterType::String, false, "Initial mode ID"},
          EDITOR::EditorOperationParameter{"resetOverrides", EditorOperationParameterType::Bool, false,
                                           "Restore inherited profile settings"}},
         [this](const auto& parameters) { return BindGameplayCamera(parameters); }},
        EDITOR::EditorOperationKind::Mutation, false);
    operationRegistry_.Register(
        {"camera_flow.edit",
         "Create, update, or remove a scene camera flow: region entry plays a sequence then changes the gameplay base mode. Scene undo and runtime projection are shared with human editing. Read camera_flow.describe for defaults and live state.",
         "scene",
         {{"action", EditorOperationParameterType::String, true, "create, update, remove"},
          {"objectId", EditorOperationParameterType::Int, false, "Existing flow scene object"},
          {"name", EditorOperationParameterType::String, false, "Display name"},
          {"settings", EditorOperationParameterType::Json, false, "Partial CameraFlowComponent settings"},
          {"position", EditorOperationParameterType::Json, false, "Local XYZ position"},
          {"rotation", EditorOperationParameterType::Json, false, "Local XYZ Euler degrees"}},
         [this](const auto& p) { return EditCameraFlow(p); }},
        EDITOR::EditorOperationKind::Mutation, false);
    EDITOR::RegisterCinematicsOperations(operationRegistry_, CinematicsWorkspace(),
                                         [this]() { return aiOperationScene_; });
    EDITOR::RegisterModelMaterialOperations(operationRegistry_, ModelMaterialWorkspace(),
                                            [this]() { return aiOperationScene_; });
    EDITOR::RegisterRuntimeBindingOperations(operationRegistry_, RuntimeBindingWorkspace(),
                                             [this]() { return aiOperationScene_; });
    EDITOR::RegisterVfxOperations(operationRegistry_, VfxWorkspace(), [this]() -> AssetDatabase* {
        return aiOperationScene_ != nullptr ? &aiOperationScene_->GetAssetDatabase() : nullptr;
    });
    EDITOR::RegisterDialogueOperations(operationRegistry_, DialogueWorkspace(), workspaceHost_,
                                       [this]() { return aiOperationScene_; });
    EDITOR::RegisterComponentAuthoringOperations(
        operationRegistry_,
        EDITOR::ComponentAuthoringOperationContext{
            [this]() { return aiOperationScene_; },
            [this](const DocumentSceneBase& scene) { return context_.sceneDirty || scene.HasUnsavedSceneChanges(); },
            [this](DocumentSceneBase& scene, SceneObjectAuthoringHistoryRequest history) {
                CommitSceneObjectAuthoringHistory(scene, std::move(history));
            },
            [this](DocumentSceneBase& scene, SceneObjectId objectId) {
                SelectViewportObjects(scene, {objectId}, EditorObjectSelectionMode::Replace);
            },
            &sceneInspectorPanel_.GetModelMaterialSlotProvider()});
}

} // namespace HIKARI
