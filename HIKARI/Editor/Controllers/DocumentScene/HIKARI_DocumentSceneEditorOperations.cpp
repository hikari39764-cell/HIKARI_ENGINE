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

#include <utility>

namespace HIKARI {

    namespace {

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

    } // namespace

    void DocumentSceneEditorController::RegisterEditorOperations() {
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
            "Set one object's transform by stable objectId. Exact name is a discovery fallback and must be unique.",
            "scene",
            {
                P("objectId", EditorOperationParameterType::Int,
                  false, "scene.objects.query 返回的稳定 SceneObjectId"),
                P("name", EditorOperationParameterType::String,
                  false, "仅兼容发现；重名时必须改用 objectId"),
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
            "Delete the selected object, or target one object by stable objectId.",
            "scene",
            {
                P("objectId", EditorOperationParameterType::Int,
                  false, "scene.objects.query 返回的稳定 SceneObjectId"),
                P("name", EditorOperationParameterType::String,
                  false, "仅兼容发现；重名时必须改用 objectId"),
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteSceneDeleteObject(parameters);
            },
        });
        operationRegistry_.Register({
            "scene.duplicate_object",
            "Duplicate the selected object, or target one object by stable objectId.",
            "scene",
            {
                P("objectId", EditorOperationParameterType::Int,
                  false, "scene.objects.query 返回的稳定 SceneObjectId"),
                P("name", EditorOperationParameterType::String,
                  false, "仅兼容发现；重名时必须改用 objectId"),
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
        operationRegistry_.Register({
            "terrain.fill_layer",
            "Fill an entire terrain object with one material layer while preserving authored height and coverage.",
            "terrain_surface",
            {
                P("objectId", EditorOperationParameterType::Int,
                  false, "scene.objects.query 返回的稳定 SceneObjectId"),
                P("name", EditorOperationParameterType::String,
                  false, "仅兼容发现；重名时必须改用 objectId"),
                P("presetId", EditorOperationParameterType::String,
                  true, "要填充的地形材质层 preset id"),
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteAiTerrainFillLayer(parameters);
            },
        });
        operationRegistry_.Register({
            "terrain.generate_procedural",
            "Generate deterministic hills, islands, ridges, basins, or dunes into a TerrainSurface range.",
            "terrain_surface",
            {
                P("objectId", EditorOperationParameterType::Int,
                  false, "scene.objects.query 返回的稳定 SceneObjectId"),
                P("name", EditorOperationParameterType::String,
                  false, "仅兼容发现；重名时必须改用 objectId"),
                P("mode", EditorOperationParameterType::String,
                  true, "noise_hills/island_coast/ridge/basin_crater/dunes"),
                P("blendMode", EditorOperationParameterType::String,
                  false, "add/replace/max，默认 add"),
                P("seed", EditorOperationParameterType::Int,
                  false, "确定性生成种子"),
                P("centerX", EditorOperationParameterType::Float,
                  false, "地形本地 X 中心"),
                P("centerZ", EditorOperationParameterType::Float,
                  false, "地形本地 Z 中心"),
                P("extentX", EditorOperationParameterType::Float,
                  false, "本地 X 半范围"),
                P("extentZ", EditorOperationParameterType::Float,
                  false, "本地 Z 半范围"),
                P("amplitude", EditorOperationParameterType::Float,
                  false, "高度振幅"),
                P("baseHeight", EditorOperationParameterType::Float,
                  false, "替换模式基准高度"),
                P("scale", EditorOperationParameterType::Float,
                  false, "特征尺度"),
                P("edgeFalloff", EditorOperationParameterType::Float,
                  false, "边缘衰减"),
                P("blendAmount", EditorOperationParameterType::Float,
                  false, "混合量"),
                P("directionDegrees", EditorOperationParameterType::Float,
                  false, "脊线/沙丘方向"),
                P("octaves", EditorOperationParameterType::Int,
                  false, "噪声层数"),
            },
            [this](const nlohmann::json& parameters) {
                return ExecuteAiTerrainProceduralGenerate(parameters);
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
        EDITOR::RegisterRuntimeBindingOperations(
            operationRegistry_,
            runtimeBindingWorkspaceController_,
            [this]() { return aiOperationScene_; });
        EDITOR::RegisterVfxOperations(
            operationRegistry_,
            vfxWorkspaceController_,
            [this]() -> AssetDatabase* {
                return aiOperationScene_ != nullptr
                    ? &aiOperationScene_->GetAssetDatabase()
                    : nullptr;
            });
        EDITOR::RegisterDialogueOperations(
            operationRegistry_,
            dialogueWorkspaceController_,
            [this]() { return aiOperationScene_; });
        EDITOR::RegisterComponentAuthoringOperations(
            operationRegistry_,
            EDITOR::ComponentAuthoringOperationContext{
                [this]() { return aiOperationScene_; },
                [this](const DocumentSceneBase& scene) {
                    return context_.sceneDirty ||
                        scene.HasUnsavedSceneChanges();
                },
                [this](
                    DocumentSceneBase& scene,
                    SceneObjectAuthoringHistoryRequest history) {
                    CommitSceneObjectAuthoringHistory(
                        scene,
                        std::move(history));
                },
                [this](
                    DocumentSceneBase& scene,
                    SceneObjectId objectId) {
                    SelectViewportObjects(
                        scene,
                        { objectId },
                        EditorObjectSelectionMode::Replace);
                },
                &sceneInspectorPanel_.GetModelMaterialSlotProvider()
            });
    }

} // namespace HIKARI
