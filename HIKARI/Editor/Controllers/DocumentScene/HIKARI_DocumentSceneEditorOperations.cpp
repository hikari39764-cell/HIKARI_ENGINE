#include "Editor/Preview/HIKARI_EditorPreviewCapture.h"
#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include "Editor/AI/HIKARI_AI_ProjectKnowledge.h"
#include "Editor/Assets/HIKARI_AssetImportSettingsService.h"
#include "Editor/Authoring/HIKARI_ComponentAuthoringOperations.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Quality/HIKARI_GlobalPostOperations.h"
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

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace HIKARI {
namespace {
using EDITOR::EditorOperationKind;
using EDITOR::EditorOperationParameter;
using EDITOR::EditorOperationParameterType;
using Parameters = std::vector<EditorOperationParameter>;

Parameters Concat(std::initializer_list<Parameters> groups) {
    Parameters result;
    for (const Parameters& group : groups) {
        result.insert(result.end(), group.begin(), group.end());
    }
    return result;
}

// prefix + 各成分（XYZ / RGB）の Float 引数。説明は before + 成分名 + after。
Parameters ComponentParameters(std::string_view prefix, std::string_view components, std::string_view before,
                               std::string_view after = {}) {
    Parameters result;
    for (const char component : components) {
        result.push_back({std::string(prefix) + component, EditorOperationParameterType::Float, false,
                          std::string(before) + component + std::string(after)});
    }
    return result;
}

Parameters PositionParameters() { return ComponentParameters("position", "XYZ", "世界位置 "); }
Parameters RotationParameters() { return ComponentParameters("rotation", "XYZ", "欧拉旋转 ", "（度）"); }
Parameters ScaleParameters() { return ComponentParameters("scale", "XYZ", "缩放 "); }

Parameters ObjectTargetParameters() {
    return {{"objectId", EditorOperationParameterType::Int, false, "scene.objects.query 返回的稳定 SceneObjectId"},
            {"name", EditorOperationParameterType::String, false, "仅兼容发现；重名时必须改用 objectId"}};
}

Parameters NameParameter(const char* description) {
    return {{"name", EditorOperationParameterType::String, false, description}};
}

EDITOR::EditorOperationResult MaterialNodeQueryResult(bool ok, std::string message, std::vector<std::string> details,
                                                      std::string code) {
    return {ok, ok ? "OK" : std::move(code), std::move(message), nlohmann::json::object(), std::move(details)};
}

struct SceneObjectOperationSpec {
    const char* id;
    const char* summary;
    const char* workspace;
    EDITOR::SceneObjectOperation operation;
    Parameters parameters;
    EditorOperationKind kind = EditorOperationKind::Mutation;
};

std::vector<SceneObjectOperationSpec> SceneObjectOperationSpecs() {
    return {
        {"scene.set_transform",
         "Set one object's transform by stable objectId. Exact name is a discovery fallback and must be unique.",
         "scene", EDITOR::SetSceneObjectTransform,
         Concat({ObjectTargetParameters(), PositionParameters(), ScaleParameters(), RotationParameters()})},
        {"scene.create_object", "Create a model object in the scene by asset id or path fragment.", "scene",
         EDITOR::CreateSceneModel,
         Concat({{{"model", EditorOperationParameterType::String, true, "模型资产 id 或路径片段"}},
                 NameParameter("物体名称，缺省为 AI Model"),
                 PositionParameters(),
                 ScaleParameters()})},
        {"scene.create_world_sprite", "Create a 3D world sprite directly from a texture asset id or path fragment.",
         "scene", EDITOR::CreateSceneWorldSprite,
         Concat({{{"texture", EditorOperationParameterType::String, true, "纹理资产 id 或路径片段"}},
                 NameParameter("物体名称，缺省使用纹理名"),
                 PositionParameters(),
                 ScaleParameters()})},
        {"scene.create_world_text", "Create editable SDF text directly in 3D world space.", "scene",
         EDITOR::CreateSceneWorldText,
         Concat(
             {{{"text", EditorOperationParameterType::String, true, "UTF-8 文本内容"},
               {"fontAssetPath", EditorOperationParameterType::String, false, "项目 Fonts 或 Assets 下的相对字体路径"}},
              NameParameter("物体名称，缺省为 World Text"),
              PositionParameters(),
              RotationParameters(),
              ScaleParameters(),
              {{"layoutWidth", EditorOperationParameterType::Float, false, "文本布局宽度（世界单位）"},
               {"layoutHeight", EditorOperationParameterType::Float, false, "文本布局高度（世界单位）"},
               {"fontHeight", EditorOperationParameterType::Float, false, "字高（世界单位）"}},
              ComponentParameters("color", "RGB", "文字线性色彩 "),
              ComponentParameters("outline", "RGB", "描边线性色彩 "),
              {{"outlineWidth", EditorOperationParameterType::Float, false, "SDF 描边宽度，范围 0-0.25"},
               {"opacity", EditorOperationParameterType::Float, false, "不透明度，范围 0-1"},
               {"horizontalAlign", EditorOperationParameterType::String, false, "left/center/right"},
               {"verticalAlign", EditorOperationParameterType::String, false, "top/center/bottom"},
               {"wrap", EditorOperationParameterType::String, false, "none/character"},
               {"facing", EditorOperationParameterType::String, false, "fixed/camera_facing/fixed_y_axis"}}})},
        {"procedural.create_primitive",
         "Create a procedural mesh object (box/sphere/plane/grid_plane/cylinder/capsule).", "scene",
         EDITOR::CreateScenePrimitive,
         Concat(
             {{{"kind", EditorOperationParameterType::String, false, "box/sphere/plane/grid_plane/cylinder/capsule"}},
              NameParameter("物体名称，缺省使用 kind"),
              PositionParameters(),
              ScaleParameters(),
              {{"width", EditorOperationParameterType::Float, false, "网格宽度"},
               {"height", EditorOperationParameterType::Float, false, "网格高度"},
               {"depth", EditorOperationParameterType::Float, false, "网格深度"}}})},
        {"scene.delete_object", "Delete the selected object, or target one object by stable objectId.", "scene",
         EDITOR::DeleteSceneObjects, ObjectTargetParameters()},
        {"scene.duplicate_object", "Duplicate the selected object, or target one object by stable objectId.", "scene",
         EDITOR::DuplicateSceneObjects, ObjectTargetParameters()},
        {"scene.create_empty_object", "Create an empty scene object.", "scene", EDITOR::CreateSceneEmptyObject,
         Concat({NameParameter("物体名称，缺省为 AI Object"), PositionParameters(), ScaleParameters()})},
    };
}

Parameters ProceduralTerrainParameters() {
    return Concat(
        {ObjectTargetParameters(),
         {{"mode", EditorOperationParameterType::String, true, "noise_hills/island_coast/ridge/basin_crater/dunes"},
          {"blendMode", EditorOperationParameterType::String, false, "add/replace/max，默认 add"},
          {"seed", EditorOperationParameterType::Int, false, "确定性生成种子"},
          {"centerX", EditorOperationParameterType::Float, false, "地形本地 X 中心"},
          {"centerZ", EditorOperationParameterType::Float, false, "地形本地 Z 中心"},
          {"extentX", EditorOperationParameterType::Float, false, "本地 X 半范围"},
          {"extentZ", EditorOperationParameterType::Float, false, "本地 Z 半范围"},
          {"amplitude", EditorOperationParameterType::Float, false, "高度振幅"},
          {"baseHeight", EditorOperationParameterType::Float, false, "替换模式基准高度"},
          {"scale", EditorOperationParameterType::Float, false, "特征尺度"},
          {"edgeFalloff", EditorOperationParameterType::Float, false, "边缘衰减"},
          {"blendAmount", EditorOperationParameterType::Float, false, "混合量"},
          {"directionDegrees", EditorOperationParameterType::Float, false, "脊线/沙丘方向"},
          {"octaves", EditorOperationParameterType::Int, false, "噪声层数"}}});
}
} // namespace

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
        EditorOperationKind::Session, false);

    for (SceneObjectOperationSpec& spec : SceneObjectOperationSpecs()) {
        operationRegistry_.Register({spec.id, spec.summary, spec.workspace, std::move(spec.parameters),
                                     [this, operation = spec.operation](const nlohmann::json& parameters) {
                                         return ExecuteSceneObjectOperation(operation, parameters);
                                     }},
                                    spec.kind);
    }
    operationRegistry_.Register({"scene.save",
                                 "Save the current scene document.",
                                 "scene",
                                 {},
                                 [this](const nlohmann::json&) { return ExecuteSceneSave(); }},
                                EditorOperationKind::Mutation);
    operationRegistry_.Register({"action.undo_last",
                                 "Undo the most recent edit in the active workspace.",
                                 "general",
                                 {},
                                 [this](const nlohmann::json&) { return ExecuteAiUndoLast(); }},
                                EditorOperationKind::Mutation);

    EDITOR::RegisterAssetImportSettingsOperations(operationRegistry_, [this]() -> AssetDatabase* {
        return aiOperationScene_ ? &aiOperationScene_->GetAssetDatabase() : nullptr;
    });

    operationRegistry_.Register({"asset.import",
                                 "Copy a supported source file into the project and queue its import.",
                                 "assets",
                                 {{"path", EditorOperationParameterType::String, true, "源文件绝对路径"}},
                                 [this](const nlohmann::json& parameters) { return ExecuteAiAssetImport(parameters); }},
                                EditorOperationKind::Mutation, false);
    operationRegistry_.Register({"asset.import_status",
                                 "Report the current queued asset import progress.",
                                 "assets",
                                 {},
                                 [this](const nlohmann::json&) { return ExecuteAiAssetImportStatus(); }},
                                EditorOperationKind::Query, false);

    operationRegistry_.Register(
        {"scene.object.focus",
         "Select and frame one object in the Scene workspace.",
         "scene",
         {{"objectId", EditorOperationParameterType::Int, true, "Stable scene object id"}},
         [this](const nlohmann::json& p) {
             if (!aiOperationScene_)
                 return EDITOR::OperationFailure("NO_SCENE", "No scene is open.");
             const SceneObjectId id{p.at("objectId").get<uint64_t>()};
             if (!aiOperationScene_->GetWorld().FindObject(id))
                 return EDITOR::OperationFailure("OBJECT_NOT_FOUND", "Object no longer exists.");
             SelectViewportObjects(*aiOperationScene_, {id}, EditorObjectSelectionMode::Replace);
             EDITOR::EditorWorkspaceOpenRequest request;
             request.workspaceId = EDITOR::EditorWorkspaceId::Scene;
             workspaceHost_.RequestOpen(request);
             sceneInspectorPanel_.RequestFocusObject(id);
             return EDITOR::OperationOk("Object selected; scene framing requested.");
         }},
        EditorOperationKind::Session, false);

    EDITOR::EditorOperationDefinition captureViews{
        "scene.capture_views",
        "Render the current scene from up to 6 fixed angles (front, three_quarter, right, back, left, top) around an object or point and return all images.",
        "scene",
        {{"focusObjectId", EditorOperationParameterType::Int, false,
          "Object to orbit; defaults to the scene camera target"},
         {"center", EditorOperationParameterType::Json, false, "World [x, y, z] center instead of focusObjectId"},
         {"views", EditorOperationParameterType::Json, false, "Angle names; default front, three_quarter, right, top"},
         {"distance", EditorOperationParameterType::Float, false,
          "Camera distance; defaults to object bounds fit, or 6 for a point"},
         {"pitchDegrees", EditorOperationParameterType::Float, false, "Elevation for side views, default 20"},
         {"fovDegrees", EditorOperationParameterType::Float, false, "Vertical field of view, default 50"},
         {"width", EditorOperationParameterType::Int, false, "Image width 128-1536"},
         {"height", EditorOperationParameterType::Int, false, "Image height 128-1536"}},
        [this](const nlohmann::json& parameters) -> EDITOR::EditorOperationResult {
            if (!aiOperationScene_)
                return EDITOR::OperationFailure("NO_SCENE", "No scene is open.");
            return sceneViewSeries_.Start(*aiOperationScene_, parameters);
        }};
    captureViews.entryPoint = true;
    captureViews.searchTerms = {"screenshot", "capture", "angles", "multi view", "截图", "多角度"};
    operationRegistry_.Register(std::move(captureViews), EditorOperationKind::Session);
    operationRegistry_.Register({"scene.capture_views.status",
                                 "Poll a multi-angle scene capture.",
                                 "scene",
                                 {},
                                 [this](const nlohmann::json&) { return sceneViewSeries_.Poll(aiOperationScene_); }},
                                EditorOperationKind::Query, false);
    operationRegistry_.Register(
        {"capture.status",
         "Poll a capture ticket and return its actual image.",
         "general",
         {{"captureId", EditorOperationParameterType::Int, true, "Capture ticket"}},
         [this](const nlohmann::json& p) { return PollAiPreviewCapture(p.at("captureId").get<uint64_t>()); }},
        EditorOperationKind::Query, false);
    operationRegistry_.Register(
        {"capture.preview",
         "Capture the actual displayed output. auto uses the game/output monitor for scene and cameras; space explicitly captures the camera workspace observer. Poll capture.status.",
         "general",
         {{"path", EditorOperationParameterType::String, false, "输出 PNG 路径，可省略"},
          {"source", EditorOperationParameterType::String, false, "auto (default), game, space or preview"}},
         [this](const nlohmann::json& parameters) { return ExecuteAiCapturePreview(parameters); }},
        EditorOperationKind::Session, false);

    operationRegistry_.Register(
        {"material.node_info",
         "Return one node's semantic contract.",
         "model_material",
         {{"nodeType", EditorOperationParameterType::String, true, "稳定节点类型名，例如 node.additive"}},
         [](const nlohmann::json& parameters) {
             std::string message;
             std::string code;
             std::vector<std::string> details;
             const bool ok =
                 EDITOR::DescribeAiMaterialNode(parameters.value("nodeType", std::string{}), message, details, code);
             return MaterialNodeQueryResult(ok, std::move(message), std::move(details), std::move(code));
         }},
        EditorOperationKind::Query, false);
    operationRegistry_.Register(
        {"material.node_list",
         "List discoverable material graph node types.",
         "model_material",
         {{"keyword", EditorOperationParameterType::String, false, "按关键字过滤节点类型"},
          {"class", EditorOperationParameterType::String, false, "按节点类别过滤"}},
         [](const nlohmann::json& parameters) {
             std::string message;
             std::string code;
             std::vector<std::string> details;
             const bool ok =
                 EDITOR::ListAiMaterialNodes(parameters.value("keyword", std::string{}),
                                             parameters.value("class", std::string{}), message, details, code);
             return MaterialNodeQueryResult(ok, std::move(message), std::move(details), std::move(code));
         }},
        EditorOperationKind::Query, false);

    operationRegistry_.Register(
        {"terrain.fill_layer",
         "Fill an entire terrain object with one material layer while preserving authored height and coverage.",
         "terrain_surface",
         Concat({ObjectTargetParameters(),
                 {{"presetId", EditorOperationParameterType::String, true, "要填充的地形材质层 preset id"}}}),
         [this](const nlohmann::json& parameters) {
             return ExecuteTerrainOperation(EDITOR::FillTerrainLayer, parameters);
         }},
        EditorOperationKind::Mutation);
    operationRegistry_.Register(
        {"terrain.describe",
         "Describe a terrain: world bounds, surface presets, stroke tools and brush, plus heights at optional sample points.",
         "terrain_surface",
         Concat({ObjectTargetParameters(),
                 {{"samples", EditorOperationParameterType::Json, false, "Optional world [x, z] points to sample"}}}),
         [this](const nlohmann::json& parameters) {
             return ExecuteTerrainOperation(EDITOR::DescribeTerrain, parameters);
         }},
        EditorOperationKind::Query);
    operationRegistry_.Register(
        {"terrain.stroke",
         "Sculpt, paint a surface layer, cut holes (erase) or restore coverage along a world-space path or over a rectangle. One undo step.",
         "terrain_surface",
         Concat({ObjectTargetParameters(),
                 {{"tool", EditorOperationParameterType::String, true, "Sculpt mode, paint, erase or restore"},
                  {"presetId", EditorOperationParameterType::String, false, "Surface layer for paint"},
                  {"points", EditorOperationParameterType::Json, false, "Path of world [x, z] points"},
                  {"rect", EditorOperationParameterType::Json, false, "Area {\"min\":[x,z],\"max\":[x,z]}"},
                  {"radius", EditorOperationParameterType::Float, false, "Brush radius in meters"},
                  {"strength", EditorOperationParameterType::Float, false, "Brush strength"},
                  {"hardness", EditorOperationParameterType::Float, false, "Brush hardness 0-1"},
                  {"falloff", EditorOperationParameterType::Float, false, "Brush falloff 0-1"},
                  {"flattenHeight", EditorOperationParameterType::Float, false, "Target height for flatten"},
                  {"passes", EditorOperationParameterType::Int, false, "Repeat the stroke 1-16 times"}}}),
         [this](const nlohmann::json& parameters) {
             return ExecuteTerrainOperation(EDITOR::StrokeTerrain, parameters);
         }},
        EditorOperationKind::Mutation);
    operationRegistry_.Register(
        {"terrain.generate_procedural",
         "Generate deterministic hills, islands, ridges, basins, or dunes into a TerrainSurface range.",
         "terrain_surface", ProceduralTerrainParameters(),
         [this](const nlohmann::json& parameters) {
             return ExecuteTerrainOperation(EDITOR::GenerateTerrain, parameters);
         }},
        EditorOperationKind::Mutation);

    RegisterEnvironmentOperations();
    EDITOR::RegisterGlobalPostOperations(operationRegistry_, qualityPanel_.GlobalPost(),
                                         [this] { return aiOperationScene_; });
    EDITOR::RegisterProjectKnowledgeOperations(operationRegistry_, [this] { return aiOperationScene_; });
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
         {{"assetGuid", EditorOperationParameterType::String, true, "Gameplay camera asset GUID"},
          {"cameraId", EditorOperationParameterType::Int, false, "Existing camera or zero to create"},
          {"targetId", EditorOperationParameterType::Int, true, "Follow target scene object ID"},
          {"lookTargetId", EditorOperationParameterType::Int, false, "Optional lock target ID"},
          {"mode", EditorOperationParameterType::String, false, "Initial mode ID"},
          {"resetOverrides", EditorOperationParameterType::Bool, false, "Restore inherited profile settings"}},
         [this](const auto& parameters) { return BindGameplayCamera(parameters); }},
        EditorOperationKind::Mutation, false);
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
        EditorOperationKind::Mutation, false);
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
