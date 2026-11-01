#include "Editor/Authoring/HIKARI_ComponentAuthoringQueryService.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Authoring/HIKARI_SceneObjectOperations.h"
#include "Editor/Authoring/HIKARI_EditorObjectFactory.h"
#include "Editor/Authoring/HIKARI_SceneObjectTargetResolver.h"
#include "Editor/Authoring/HIKARI_SceneTransformAuthoring.h"
#include "Editor/Commands/HIKARI_SceneObjectCommandService.h"
#include "Editor/Commands/HIKARI_EditorOperationValue.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Core/HIKARI_JsonRead.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include <cstdio>

namespace HIKARI::EDITOR {
namespace {
class ObjectCreationEdit {
public:
    ObjectCreationEdit(SceneObjectOperationContext& operation, std::string label) : operation_(operation) {
        const auto& document = operation.scene.GetSceneDocument();
        history_.label = std::move(label);
        history_.beforeObjects = document.objects;
        history_.beforeCamera = document.camera;
        history_.dirtyBefore = operation.editor.sceneDirty || operation.scene.HasUnsavedSceneChanges();
    }
    // 操作で作った物は選ばない（智能体が人の選択を変えずに並行して作業できるように）。
    void Complete(GameObject&) {
        auto& scene = operation_.scene;
        operation_.selection.SyncNextSceneObjectId(scene, operation_.editor.nextSceneObjectId);
        history_.afterObjects = scene.GetSceneDocument().objects;
        history_.afterCamera = scene.GetSceneDocument().camera;
        operation_.history = std::move(history_);
    }

private:
    SceneObjectOperationContext& operation_;
    SceneObjectAuthoringHistoryRequest history_;
};

void ReadObjectTransform(const nlohmann::json& parameters, CreateObjectRequest& request) {
    request.position = JSONREAD::Vec3MemberOr(parameters, "position", {0.0f, 0.0f, 0.0f});
    request.rotation = JSONREAD::Vec3MemberOr(parameters, "rotation", {0.0f, 0.0f, 0.0f});
    request.scale = JSONREAD::Vec3MemberOr(parameters, "scale", {1.0f, 1.0f, 1.0f});
}

// 作成の受領書は要約だけ（全量は scene.objects.query の detail）。智能体の文脈を小さく保つ。
nlohmann::json CreatedObjectReceipt(const DocumentSceneBase& scene, const GameObject& object) {
    const SceneObjectData* authored = FindSceneObjectData(scene.GetSceneDocument(), object.GetDocumentId());
    nlohmann::json receipt{{"objectId", object.GetDocumentId().value}};
    if (authored != nullptr)
        receipt["object"] = ComponentAuthoringQueryService{}.SummarizeObject(scene, *authored);
    return receipt;
}

EditorOperationResult EditSceneSelection(SceneObjectOperationContext& operation, SceneObjectCommandId command,
                                         const nlohmann::json& parameters) {
    EDITOR::EditorOperationResult result{};
    DocumentSceneBase* scene = &operation.scene;
    const std::string name = parameters.value("name", std::string{});
    std::optional<SceneObjectId> objectId{};
    if (!ReadOptionalSceneObjectId(parameters, objectId, result)) {
        return result;
    }
    // 対象を指定された時は一時的に選んで選択に対する命令を使い、終わったら人の選択に戻す
    // （智能体が人の選択を変えずに並行して作業できるように）。
    const auto previousSelection = operation.editor.selection;
    const bool targeted = objectId.has_value() || !name.empty();
    const auto restoreSelection = [&] {
        if (!targeted)
            return;
        operation.editor.selection = previousSelection;
        operation.editor.selection.RepairObjectSelection(scene->GetWorld(), previousSelection.GetActiveObjectId());
    };
    if (targeted) {
        const EDITOR::SceneObjectTargetResolution resolution = EDITOR::ResolveSceneObjectTarget(*scene, objectId, name);
        if (!resolution.IsResolved()) {
            return TargetResolutionFailure(resolution);
        }
        GameObject* target = resolution.runtimeObject;
        operation.editor.selection.SelectObject(scene->GetWorld(), target);
    }
    if (!operation.commands.CanExecute(command, *scene, operation.editor)) {
        restoreSelection();
        return OperationFailure("INVALID_STATE", "没有可操作的选中对象");
    }
    if (!operation.commands.Execute(command, *scene, operation.editor, operation.selection)) {
        restoreSelection();
        result.code = command == SceneObjectCommandId::Delete ? "DELETE_FAILED" : "DUPLICATE_FAILED";
        result.message = "对象操作失败";
        return result;
    }
    operation.history = operation.commands.ConsumeHistoryRequest();
    // 複製は新しい物を選ぶので、人の選択へ戻す前にその id を返す（探し直さずに続けて編集できる）。
    nlohmann::json data = nlohmann::json::object();
    if (command == SceneObjectCommandId::Duplicate) {
        nlohmann::json ids = nlohmann::json::array();
        for (const SceneObjectId id : operation.editor.selection.GetSelectedObjectIds())
            ids.push_back(id.value);
        data[ids.size() == 1u ? "objectId" : "objectIds"] = ids.size() == 1u ? ids[0] : ids;
    }
    restoreSelection();
    return OperationOk(operation.commands.GetStatusMessage(), std::move(data));
}
} // namespace

EditorOperationResult SetSceneObjectTransform(SceneObjectOperationContext& operation,
                                              const nlohmann::json& parameters) {
    using namespace EDITOR;
    auto* scene = &operation.scene;
    if (scene->IsRuntimePlayActive())
        return {false, "PLAY_ACTIVE", "Stop Play before editing transforms."};
    std::optional<SceneObjectId> objectId;
    EditorOperationResult failure;
    if (!ReadOptionalSceneObjectId(parameters, objectId, failure))
        return failure;
    const auto target = ResolveSceneObjectTarget(*scene, objectId, parameters.value("name", std::string{}));
    if (!target.IsResolved())
        return TargetResolutionFailure(target);
    auto command = DescribeSceneObjectTransform(*target.documentObject);
    command["objectId"] = target.documentObject->id.value;
    for (const char* channel : {"position", "rotation", "scale"}) {
        if (const auto found = parameters.find(channel); found != parameters.end())
            command[channel] = *found;
    }
    std::vector<SceneObjectData> staged;
    auto result = StageSceneTransformEdits(scene->GetSceneDocument(), nlohmann::json::array({command}), staged);
    if (!result.ok || result.code == "NO_CHANGE")
        return result;
    const auto before = scene->GetSceneDocument().objects;
    const bool dirty = operation.editor.sceneDirty || scene->HasUnsavedSceneChanges();
    const auto id = target.documentObject->id.value;
    scene->GetSceneDocument().objects = std::move(staged);
    operation.history = SceneObjectAuthoringHistoryRequest{"Edit Transform",
                                                           before,
                                                           scene->GetSceneDocument().objects,
                                                           scene->GetSceneDocument().camera,
                                                           scene->GetSceneDocument().camera,
                                                           dirty,
                                                           true,
                                                           true,
                                                           parameters.value("mergeGroup", uint64_t{0})};
    result.message = "Object transform updated.";
    result.data = {{"objectId", id}};
    return result;
}

EditorOperationResult CreateSceneModel(SceneObjectOperationContext& operation, const nlohmann::json& parameters) {
    EDITOR::EditorOperationResult result{};
    DocumentSceneBase* scene = &operation.scene;
    const std::string model = parameters.value("model", std::string{});
    if (model.empty()) {
        result.code = "MISSING_FIELD";
        result.message = "缺少 model 字段（资产 id 或路径片段，"
                         "可用 asset.query 查询）";
        return result;
    }
    const AssetRegistry& registry = scene->GetAssetRegistry();
    const AssetDescriptor* descriptor = registry.FindDescriptor(model);
    if (descriptor == nullptr) {
        for (const AssetDescriptor* candidate : registry.CollectByType(AssetType::Model)) {
            if (candidate->sourcePath.find(model) != std::string::npos ||
                candidate->id.value.find(model) != std::string::npos) {
                descriptor = candidate;
                break;
            }
        }
    }
    if (descriptor == nullptr) {
        return OperationFailure("MODEL_NOT_FOUND", "找不到模型资产：" + model);
    }
    ObjectCreationEdit edit(operation, "Create Model Object");
    EDITOR::CreateObjectRequest request{};
    request.name = parameters.value("name", std::string{});
    if (request.name.empty()) {
        request.name = "AI Model";
    }
    ReadObjectTransform(parameters, request);
    GameObject* object = EDITOR::CreateModelObject(*scene, descriptor->id, request);
    if (object == nullptr) {
        return OperationFailure("CREATE_FAILED", "创建模型物体失败：" + request.name);
    }
    edit.Complete(*object);
    result.ok = true;
    result.data = CreatedObjectReceipt(*scene, *object);
    result.message = "已创建模型 " + request.name + "（" + descriptor->sourcePath + "）";
    return result;
}

EditorOperationResult CreateSceneWorldSprite(SceneObjectOperationContext& operation, const nlohmann::json& parameters) {
    EDITOR::EditorOperationResult result{};
    DocumentSceneBase* scene = &operation.scene;
    const std::string texture = parameters.value("texture", std::string{});
    if (texture.empty()) {
        return OperationFailure("MISSING_FIELD", "缺少 texture 字段（纹理资产 id 或路径片段）");
    }
    const AssetRegistry& registry = scene->GetAssetRegistry();
    const AssetDescriptor* descriptor = registry.FindDescriptor(texture);
    if (descriptor != nullptr && descriptor->type != AssetType::Texture) {
        descriptor = nullptr;
    }
    if (descriptor == nullptr) {
        for (const AssetDescriptor* candidate : registry.CollectByType(AssetType::Texture)) {
            if (candidate != nullptr && (candidate->sourcePath.find(texture) != std::string::npos ||
                                         candidate->id.value.find(texture) != std::string::npos)) {
                descriptor = candidate;
                break;
            }
        }
    }
    if (descriptor == nullptr) {
        return OperationFailure("TEXTURE_NOT_FOUND", "找不到纹理资产：" + texture);
    }

    ObjectCreationEdit edit(operation, "Create World Sprite");
    EDITOR::CreateObjectRequest request{};
    request.name = parameters.value("name", std::string{});
    ReadObjectTransform(parameters, request);
    GameObject* object = EDITOR::CreateWorldSpriteObject(*scene, descriptor->id, request);
    if (object == nullptr) {
        return OperationFailure("CREATE_FAILED", "创建 3D 世界贴图失败");
    }
    edit.Complete(*object);
    result.data = CreatedObjectReceipt(*scene, *object);
    return OperationOk("已创建 3D 世界贴图 " + object->GetName(), std::move(result.data));
}

EditorOperationResult CreateSceneWorldText(SceneObjectOperationContext& operation, const nlohmann::json& parameters) {
    EDITOR::EditorOperationResult result{};
    DocumentSceneBase* scene = &operation.scene;

    EDITOR::CreateWorldTextRequest request{};
    request.text = parameters.value("text", std::string{});
    if (request.text.empty()) {
        return OperationFailure("MISSING_FIELD", "缺少非空 text 字段");
    }
    request.object.name = parameters.value("name", std::string{});
    const std::string fontAssetPath = parameters.value("fontAssetPath", std::string{});
    if (!fontAssetPath.empty()) {
        request.fontAssetPath = fontAssetPath;
    }
    ReadObjectTransform(parameters, request.object);
    request.layoutSize = {
        EDITOR::ReadEditorOperationFloat(parameters, "layoutWidth", 4.0f),
        EDITOR::ReadEditorOperationFloat(parameters, "layoutHeight", 1.0f),
    };
    request.fontHeight = EDITOR::ReadEditorOperationFloat(parameters, "fontHeight", 0.5f);
    request.color = JSONREAD::Vec3MemberOr(parameters, "color", {1.0f, 1.0f, 1.0f});
    request.outlineColor = JSONREAD::Vec3MemberOr(parameters, "outline", {0.0f, 0.0f, 0.0f});
    request.outlineWidth = EDITOR::ReadEditorOperationFloat(parameters, "outlineWidth", 0.04f);
    request.opacity = EDITOR::ReadEditorOperationFloat(parameters, "opacity", 1.0f);

    const std::string horizontalAlign =
        TEXT::ToLowerAsciiCopy(parameters.value("horizontalAlign", std::string("center")));
    if (horizontalAlign == "left") {
        request.horizontalAlign = TEXT_RENDERING::SdfTextHorizontalAlign::Left;
    } else if (horizontalAlign == "right") {
        request.horizontalAlign = TEXT_RENDERING::SdfTextHorizontalAlign::Right;
    } else if (horizontalAlign != "center") {
        return OperationFailure("INVALID_FIELD", "horizontalAlign 必须为 left/center/right");
    }

    const std::string verticalAlign = TEXT::ToLowerAsciiCopy(parameters.value("verticalAlign", std::string("center")));
    if (verticalAlign == "top") {
        request.verticalAlign = TEXT_RENDERING::SdfTextVerticalAlign::Top;
    } else if (verticalAlign == "bottom") {
        request.verticalAlign = TEXT_RENDERING::SdfTextVerticalAlign::Bottom;
    } else if (verticalAlign != "center") {
        return OperationFailure("INVALID_FIELD", "verticalAlign 必须为 top/center/bottom");
    }

    const std::string wrap = TEXT::ToLowerAsciiCopy(parameters.value("wrap", std::string("character")));
    if (wrap == "none") {
        request.wrap = TEXT_RENDERING::SdfTextWrap::None;
    } else if (wrap != "character") {
        return OperationFailure("INVALID_FIELD", "wrap 必须为 none/character");
    }

    const std::string facing = TEXT::ToLowerAsciiCopy(parameters.value("facing", std::string("camera_facing")));
    if (facing == "fixed") {
        request.facing = RENDER3D::WORLD_TEXT::WorldTextFacingMode::Fixed;
    } else if (facing == "fixed_y_axis") {
        request.facing = RENDER3D::WORLD_TEXT::WorldTextFacingMode::FixedYAxis;
    } else if (facing != "camera_facing") {
        return OperationFailure("INVALID_FIELD", "facing 必须为 fixed/camera_facing/fixed_y_axis");
    }

    ObjectCreationEdit edit(operation, "Create World Text");
    GameObject* object = EDITOR::CreateWorldTextObject(*scene, request);
    if (object == nullptr) {
        return OperationFailure("CREATE_FAILED", "创建 3D 世界文字失败");
    }
    edit.Complete(*object);
    result.data = CreatedObjectReceipt(*scene, *object);
    return OperationOk("已创建 3D 世界文字 " + object->GetName(), std::move(result.data));
}

EditorOperationResult CreateScenePrimitive(SceneObjectOperationContext& operation, const nlohmann::json& parameters) {
    EDITOR::EditorOperationResult result{};
    DocumentSceneBase* scene = &operation.scene;
    const std::string kind = parameters.value("kind", std::string{});
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
    } else if (kind == "ramp") {
        meshKind = ProceduralMeshKind::Ramp;
    } else if (kind == "stairs") {
        meshKind = ProceduralMeshKind::Stairs;
    }

    EDITOR::CreatePrimitiveRequest request{};
    request.object.name = parameters.value("name", std::string{});
    if (request.object.name.empty()) {
        request.object.name = kind.empty() ? "AI Primitive" : kind;
    }
    ReadObjectTransform(parameters, request.object);
    request.mesh.kind = meshKind;
    request.mesh.width = EDITOR::ReadEditorOperationFloat(parameters, "width", 1.0f);
    request.mesh.height = EDITOR::ReadEditorOperationFloat(parameters, "height", 1.0f);
    request.mesh.depth = EDITOR::ReadEditorOperationFloat(parameters, "depth", 1.0f);
    request.mesh = SanitizeProceduralMeshSettings(request.mesh);
    ObjectCreationEdit edit(operation, "Create Primitive");
    GameObject* object = EDITOR::CreatePrimitiveObject(*scene, request);
    if (object == nullptr) {
        return OperationFailure("CREATE_FAILED", "创建程序化物体失败：" + request.object.name);
    }
    edit.Complete(*object);
    char positionText[96];
    std::snprintf(positionText, sizeof(positionText), "(%.2f, %.2f, %.2f)", request.object.position.x,
                  request.object.position.y, request.object.position.z);
    result.data = CreatedObjectReceipt(*scene, *object);
    result.ok = true;
    result.message = "已创建 " + request.object.name + " " + std::string(positionText);
    return result;
}

EditorOperationResult CreateSceneEmptyObject(SceneObjectOperationContext& operation, const nlohmann::json& parameters) {
    EDITOR::EditorOperationResult result{};
    DocumentSceneBase* scene = &operation.scene;
    ObjectCreationEdit edit(operation, "Create Empty Object");
    EDITOR::CreateObjectRequest request{};
    request.name = parameters.value("name", std::string{});
    if (request.name.empty()) {
        request.name = "AI Object";
    }
    ReadObjectTransform(parameters, request);
    GameObject* object = EDITOR::CreateEmptyObject(*scene, request);
    if (object == nullptr) {
        return OperationFailure("CREATE_FAILED", "创建空物体失败：" + request.name);
    }
    edit.Complete(*object);
    result.data = CreatedObjectReceipt(*scene, *object);
    result.ok = true;
    result.message = "已创建空物体 " + request.name;
    return result;
}
EditorOperationResult DeleteSceneObjects(SceneObjectOperationContext& operation, const nlohmann::json& parameters) {
    return EditSceneSelection(operation, SceneObjectCommandId::Delete, parameters);
}
EditorOperationResult DuplicateSceneObjects(SceneObjectOperationContext& operation, const nlohmann::json& parameters) {
    return EditSceneSelection(operation, SceneObjectCommandId::Duplicate, parameters);
}

} // namespace HIKARI::EDITOR
