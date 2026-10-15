#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Assets/Models/HIKARI_ModelBounds.h"
#include "Core/HIKARI_JsonRead.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Authoring/HIKARI_EditorObjectState.h"
#include "Editor/Authoring/HIKARI_SceneObjectObservation.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Views/HIKARI_EditorViewportGeometry.h"
#include "Editor/Widgets/HIKARI_InspectorPropertyLayout.h"
#include "Editor/Widgets/HIKARI_NumberField.h"
#include "Editor/Workspaces/LookDev/HIKARI_LookDevArrange.h"
#include "Editor/Workspaces/LookDev/HIKARI_LookDevDocument.h"
#include "Editor/Workspaces/LookDev/HIKARI_LookDevShapeHandles.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/HIKARI_SceneRuntimeBuilder.h"

#include "imgui.h"

namespace HIKARI {

namespace {

constexpr float kDegreesToRadians = 0.0174532925f;

MATH::Vec3 Rotate(const MATH::Vec3& eulerDegrees, const MATH::Vec3& vector) {
    const MATH::Quat rotation = MATH::Quat::FromEulerXYZ(
        eulerDegrees.x * kDegreesToRadians, eulerDegrees.y * kDegreesToRadians, eulerDegrees.z * kDegreesToRadians);
    const MATH::Vec4 rotated =
        MATH::Mat4::TRS({}, rotation, {1.0f, 1.0f, 1.0f}).TransformPoint({vector.x, vector.y, vector.z, 0.0f});
    return {rotated.x, rotated.y, rotated.z};
}

nlohmann::json IdList(std::span<const SceneObjectId> ids) {
    nlohmann::json list = nlohmann::json::array();
    for (SceneObjectId id : ids) {
        list.push_back(id.value);
    }
    return list;
}

// 名前の表から引く。キーが無ければ fallback、名前が表に無ければ空。
template <class E, size_t N>
std::optional<E> ParseNamed(const nlohmann::json& p, const char* key, const TEXT::EnumName<E> (&table)[N], E fallback) {
    return p.contains(key) ? TEXT::TryParseEnum(table, p.at(key).get<std::string>()) : std::optional<E>(fallback);
}

} // namespace

bool DocumentSceneEditorController::EditLookDevDocument(DocumentSceneBase& scene, std::string label,
                                                        const std::function<bool(SceneDocument&)>& edit) {
    FinishLookDevObjectDrag(scene);
    selectionSync_.SyncNextSceneObjectId(scene, context_.nextSceneObjectId);
    SceneDocument& document = scene.GetSceneDocument();
    std::vector<SceneObjectData> beforeObjects = document.objects;
    const SceneCameraSettings camera = document.camera;
    const bool dirtyBefore = context_.sceneDirty || scene.HasUnsavedSceneChanges();
    if (!edit(document)) {
        // 途中まで書き換えていても文書を元に戻す。
        document.objects = std::move(beforeObjects);
        return false;
    }
    context_.sceneDirty = true;
    selectionSync_.SyncNextSceneObjectId(scene, context_.nextSceneObjectId);
    CommitSceneObjectAuthoringHistory(
        scene, SceneObjectAuthoringHistoryRequest{std::move(label), std::move(beforeObjects), document.objects, camera,
                                                  camera, dirtyBefore, true, true});
    return true;
}

std::vector<SceneObjectId> DocumentSceneEditorController::CreateLookDevObjects(
    DocumentSceneBase& scene, std::string label, const std::function<std::vector<GameObject*>()>& create) {
    FinishLookDevObjectDrag(scene);
    selectionSync_.SyncNextSceneObjectId(scene, context_.nextSceneObjectId);
    SceneDocument& document = scene.GetSceneDocument();
    std::vector<SceneObjectData> beforeObjects = document.objects;
    const SceneCameraSettings camera = document.camera;
    const bool dirtyBefore = context_.sceneDirty || scene.HasUnsavedSceneChanges();
    std::vector<SceneObjectId> created;
    for (GameObject* object : create()) {
        if (object != nullptr) {
            created.push_back(object->GetDocumentId());
        }
    }
    if (document.objects.size() == beforeObjects.size()) {
        return {};
    }
    context_.sceneDirty = true;
    selectionSync_.SyncNextSceneObjectId(scene, context_.nextSceneObjectId);
    // 工厂は文書と実行時の両方へ足しているので、実行時の作り直しは要らない。
    CommitSceneObjectAuthoringHistory(
        scene, SceneObjectAuthoringHistoryRequest{std::move(label), std::move(beforeObjects), document.objects, camera,
                                                  camera, dirtyBefore});
    return created;
}

std::vector<EDITOR::LookDevArrangeItem> DocumentSceneEditorController::CollectLookDevArrangeItems(
    DocumentSceneBase& scene, std::span<const SceneObjectId> roots) {
    const SceneDocument& document = scene.GetSceneDocument();
    std::vector<EDITOR::LookDevArrangeItem> items;
    for (SceneObjectId root : roots) {
        Bounds bounds = BOUNDS::EmptyBounds();
        bool found = false;
        for (uint64_t id : CollectObjectSubtreeIds(document, root)) {
            const Bounds objectBounds = EDITOR::ObserveSceneObjectBounds(scene, SceneObjectId{id});
            if (BOUNDS::IsUsable(objectBounds)) {
                BOUNDS::Encapsulate(bounds, objectBounds);
                found = true;
            }
        }
        if (!found) {
            // 描かれない物（空の物・灯）は位置の点。
            const MATH::Vec3 position = EDITOR::LookDevTranslation(EDITOR::LookDevWorldMatrix(scene, root));
            bounds = {position, position};
        }
        items.push_back({root.value, bounds});
    }
    return items;
}

bool DocumentSceneEditorController::MoveLookDevItems(DocumentSceneBase& scene, std::string label,
                                                     const std::vector<EDITOR::LookDevArrangeItem>& items,
                                                     const std::vector<MATH::Vec3>& moves) {
    return EditLookDevDocument(scene, std::move(label), [&](SceneDocument& document) {
        bool changed = false;
        for (size_t index = 0; index < items.size() && index < moves.size(); ++index) {
            SceneObjectData* object = FindSceneObjectData(document, SceneObjectId{items[index].id});
            if (object == nullptr || MATH::Length(moves[index]) < 1.0e-6f ||
                EDITOR::IsObjectEditorLocked(document, object->id)) {
                continue;
            }
            object->transform.position =
                object->transform.position + EDITOR::LookDevParentLocalVector(scene, *object, moves[index]);
            changed = true;
        }
        return changed;
    });
}

std::vector<SceneObjectId> DocumentSceneEditorController::CloneLookDevObjects(DocumentSceneBase& scene,
                                                                              std::string label,
                                                                              std::span<const SceneObjectId> roots,
                                                                              size_t copies,
                                                                              const LookDevClonePlacement& place) {
    std::vector<SceneObjectId> created;
    const bool committed = EditLookDevDocument(scene, std::move(label), [&](SceneDocument& document) {
        std::unordered_set<uint64_t> subtree;
        for (SceneObjectId root : roots) {
            const auto ids = CollectObjectSubtreeIds(document, root);
            subtree.insert(ids.begin(), ids.end());
        }
        std::vector<SceneObjectData> source;
        for (const SceneObjectData& object : document.objects) {
            if (subtree.contains(object.id.value)) {
                source.push_back(object);
            }
        }
        if (source.empty()) {
            return false;
        }
        for (size_t copy = 0; copy < copies; ++copy) {
            std::unordered_map<uint64_t, uint64_t> remapped;
            if (!AppendClonedSceneObjects(source, document, context_.nextSceneObjectId, remapped)) {
                return false;
            }
            place(copy, document, remapped, roots);
            for (SceneObjectId root : roots) {
                created.push_back(SceneObjectId{remapped.at(root.value)});
            }
        }
        return true;
    });
    if (!committed) {
        created.clear();
    }
    return created;
}

std::vector<SceneObjectId> DocumentSceneEditorController::CreateLookDevArray(
    DocumentSceneBase& scene, std::span<const SceneObjectId> roots, const EDITOR::LookDevArraySettings& settings) {
    const std::vector<EDITOR::LookDevArrayCopy> copies = EDITOR::BuildLookDevArray(settings);
    const Bounds bounds = EDITOR::UnionLookDevBounds(CollectLookDevArrangeItems(scene, roots));
    const MATH::Vec3 center = (bounds.min + bounds.max) * 0.5f;
    // 環形の円の中心は、選んだ物の中心から -X へ半径だけ離れた点。
    const MATH::Vec3 ringCenter{center.x - settings.radius, center.y, center.z};
    return CloneLookDevObjects(
        scene, "Array", roots, copies.size(),
        [&](size_t copy, SceneDocument& document, const std::unordered_map<uint64_t, uint64_t>& remapped,
            std::span<const SceneObjectId> sourceRoots) {
            const EDITOR::LookDevArrayCopy& placement = copies[copy];
            for (SceneObjectId sourceRoot : sourceRoots) {
                SceneObjectData* root = FindSceneObjectData(document, SceneObjectId{remapped.at(sourceRoot.value)});
                if (root == nullptr) {
                    continue;
                }
                const MATH::Vec3 position = EDITOR::LookDevTranslation(EDITOR::LookDevWorldMatrix(scene, sourceRoot));
                MATH::Vec3 target = position + placement.offset;
                if (settings.kind == EDITOR::LookDevArrayKind::Ring) {
                    const MATH::Vec3 local = position - ringCenter;
                    const float radians = placement.yawDegrees * kDegreesToRadians;
                    target = ringCenter + MATH::Vec3{local.x * std::cos(radians) + local.z * std::sin(radians), local.y,
                                                     -local.x * std::sin(radians) + local.z * std::cos(radians)};
                }
                root->transform.position =
                    root->transform.position + EDITOR::LookDevParentLocalVector(scene, *root, target - position);
                root->transform.rotationEulerDeg.y += placement.yawDegrees;
                root->name += " " + std::to_string(copy + 2u);
            }
        });
}

std::vector<SceneObjectId> DocumentSceneEditorController::MirrorLookDevObjects(DocumentSceneBase& scene,
                                                                               std::span<const SceneObjectId> roots,
                                                                               int axis) {
    const Bounds bounds = EDITOR::UnionLookDevBounds(CollectLookDevArrangeItems(scene, roots));
    const MATH::Vec3 center = (bounds.min + bounds.max) * 0.5f;
    return CloneLookDevObjects(
        scene, "Mirror Copy", roots, 1u,
        [&](size_t, SceneDocument& document, const std::unordered_map<uint64_t, uint64_t>& remapped,
            std::span<const SceneObjectId>) {
            // 複製した物をすべて世界で鏡に映す（位置と向き、拡縮はそのまま）。親が先に決まるよう何度か回し、
            // 子は鏡に映した親に対する局所へ戻す。形そのものは裏返らない（左右対称の白模は見た目どおり）。
            std::unordered_map<uint64_t, MATH::Mat4> mirroredWorld;
            std::unordered_map<uint64_t, uint64_t> cloneToSource;
            for (const auto& [source, clone] : remapped) {
                cloneToSource.emplace(clone, source);
            }
            for (size_t pass = 0; pass < remapped.size() && mirroredWorld.size() < remapped.size(); ++pass) {
                for (const auto& [source, cloneId] : remapped) {
                    SceneObjectData* clone = FindSceneObjectData(document, SceneObjectId{cloneId});
                    if (clone == nullptr || mirroredWorld.contains(cloneId)) {
                        continue;
                    }
                    const bool parentCloned = clone->parent && cloneToSource.contains(clone->parent->value);
                    if (parentCloned && !mirroredWorld.contains(clone->parent->value)) {
                        continue;
                    }
                    MATH::Vec3 translation{};
                    MATH::Quat rotation = MATH::Quat::Identity();
                    MATH::Vec3 scale{1.0f, 1.0f, 1.0f};
                    if (!MATH::DecomposeTRS(EDITOR::LookDevWorldMatrix(scene, SceneObjectId{source}), translation,
                                            rotation, scale)) {
                        mirroredWorld.emplace(cloneId, MATH::Mat4::Identity());
                        continue;
                    }
                    const MATH::Vec3 euler =
                        EDITOR::MirrorLookDevRotation(MATH::EulerXYZDegreesFromQuat(rotation), axis);
                    const MATH::Mat4 world = MATH::Mat4::TRS(EDITOR::MirrorLookDevPosition(translation, center, axis),
                                                             MATH::Quat::FromEulerXYZ(euler.x * kDegreesToRadians,
                                                                                      euler.y * kDegreesToRadians,
                                                                                      euler.z * kDegreesToRadians),
                                                             scale);
                    mirroredWorld.emplace(cloneId, world);
                    // 親（複製した親なら鏡に映した後、外の親ならそのまま）に対する局所。
                    MATH::Mat4 parentWorld = MATH::Mat4::Identity();
                    if (parentCloned) {
                        parentWorld = mirroredWorld.at(clone->parent->value);
                    } else if (clone->parent) {
                        parentWorld = EDITOR::LookDevWorldMatrix(scene, *clone->parent);
                    }
                    MATH::Vec3 localTranslation{};
                    MATH::Quat localRotation = MATH::Quat::Identity();
                    MATH::Vec3 localScale{1.0f, 1.0f, 1.0f};
                    if (MATH::DecomposeTRS(MATH::Inverse(parentWorld) * world, localTranslation, localRotation,
                                           localScale)) {
                        clone->transform.position = localTranslation;
                        clone->transform.rotationEulerDeg =
                            MATH::EulerXYZDegreesFromQuatNearest(localRotation, clone->transform.rotationEulerDeg);
                        clone->transform.scale = localScale;
                    }
                }
            }
        });
}

SceneObjectId DocumentSceneEditorController::GroupLookDevObjects(DocumentSceneBase& scene,
                                                                 std::span<const SceneObjectId> roots,
                                                                 const std::string& name) {
    const std::vector<EDITOR::LookDevArrangeItem> items = CollectLookDevArrangeItems(scene, roots);
    const Bounds bounds = EDITOR::UnionLookDevBounds(items);
    // 組の基点は全体の底の中心（床に置いた物をまとめて動かしやすい）。
    const MATH::Vec3 pivot{(bounds.min.x + bounds.max.x) * 0.5f, bounds.min.y, (bounds.min.z + bounds.max.z) * 0.5f};
    SceneObjectId group{};
    EditLookDevDocument(scene, "Group", [&](SceneDocument& document) {
        SceneObjectData parent{};
        parent.id = SceneObjectId{context_.nextSceneObjectId++};
        parent.name = name;
        parent.transform.position = pivot;
        size_t insertAt = document.objects.size();
        for (const EDITOR::LookDevArrangeItem& item : items) {
            SceneObjectData* object = FindSceneObjectData(document, SceneObjectId{item.id});
            if (object == nullptr) {
                continue;
            }
            MATH::Vec3 translation{};
            MATH::Quat rotation = MATH::Quat::Identity();
            MATH::Vec3 scale{1.0f, 1.0f, 1.0f};
            if (!MATH::DecomposeTRS(EDITOR::LookDevWorldMatrix(scene, object->id), translation, rotation, scale)) {
                continue;
            }
            object->parent = parent.id;
            object->transform.position = translation - pivot;
            object->transform.rotationEulerDeg =
                MATH::EulerXYZDegreesFromQuatNearest(rotation, object->transform.rotationEulerDeg);
            object->transform.scale = scale;
            insertAt = (std::min)(insertAt, static_cast<size_t>(object - document.objects.data()));
        }
        group = parent.id;
        document.objects.insert(document.objects.begin() + static_cast<std::ptrdiff_t>(insertAt), std::move(parent));
        return true;
    });
    return group;
}

EDITOR::EditorOperationResult DocumentSceneEditorController::ArrangeLookDevObjects(const nlohmann::json& p) {
    DocumentSceneBase* scene = aiOperationScene_;
    if (scene == nullptr || scene->IsRuntimePlayActive()) {
        return EDITOR::OperationFailure("EDIT_MODE_REQUIRED", "Arrange objects outside Play mode.");
    }
    const std::optional<EDITOR::LookDevArrangeAction> action =
        TEXT::TryParseEnum(EDITOR::kLookDevArrangeActionNames, p.at("action").get<std::string>());
    if (!action) {
        return EDITOR::OperationFailure("INVALID_ACTION", "action is align, distribute, drop, group, mirror or array.");
    }
    const SceneDocument& document = scene->GetSceneDocument();
    std::vector<SceneObjectId> ids;
    if (p.contains("objectIds")) {
        for (const nlohmann::json& id : p.at("objectIds")) {
            ids.push_back(SceneObjectId{id.get<uint64_t>()});
            if (FindSceneObjectData(document, ids.back()) == nullptr) {
                return EDITOR::OperationFailure("OBJECT_NOT_FOUND",
                                                "Object " + std::to_string(ids.back().value) + " does not exist.");
            }
        }
    } else {
        ids = context_.selection.GetSelectedObjectIds();
    }
    // 子は親と一緒に動くので、根だけを並べる。
    const std::vector<SceneObjectId> roots = CollectRootObjectIds(document, ids);
    const size_t needed =
        *action == EDITOR::LookDevArrangeAction::Distribute                                                ? 3u
        : *action == EDITOR::LookDevArrangeAction::Align || *action == EDITOR::LookDevArrangeAction::Group ? 2u
                                                                                                           : 1u;
    if (roots.size() < needed) {
        return EDITOR::OperationFailure("TOO_FEW_OBJECTS", "This action needs at least " + std::to_string(needed) +
                                                               " root objects (children move with their parent).");
    }
    const std::optional<int> axis =
        ParseNamed(p, "axis", EDITOR::kLookDevAxisNames, *action == EDITOR::LookDevArrangeAction::Align ? 1 : 0);
    if (!axis) {
        return EDITOR::OperationFailure("INVALID_AXIS", "axis is x, y or z.");
    }
    const std::vector<EDITOR::LookDevArrangeItem> items = CollectLookDevArrangeItems(*scene, roots);
    const auto moved = [&](bool changed, const char* message) {
        return EDITOR::OperationOk(changed ? message : "Objects are already arranged.",
                                   {{"objectIds", IdList(roots)}, {"changed", changed}});
    };
    const auto created = [&](const std::vector<SceneObjectId>& objects, const char* message) {
        if (objects.empty()) {
            return EDITOR::OperationFailure("CREATE_FAILED", "No copies were created.");
        }
        return EDITOR::OperationOk(message, {{"createdIds", IdList(objects)}});
    };
    switch (*action) {
    case EDITOR::LookDevArrangeAction::Align: {
        const std::optional<EDITOR::LookDevAlign> anchor =
            ParseNamed(p, "anchor", EDITOR::kLookDevAlignNames, EDITOR::LookDevAlign::Center);
        if (!anchor) {
            return EDITOR::OperationFailure("INVALID_ANCHOR", "anchor is min, center or max.");
        }
        // 基準：referenceId（無ければ選択の主の物）。並べる物に無い referenceId はその物自身の外接箱、
        // どちらも無ければ最初の物。
        const SceneObjectId reference{p.value("referenceId", context_.selection.GetActiveObjectId().value)};
        const auto found =
            std::find_if(items.begin(), items.end(), [&](const auto& item) { return item.id == reference.value; });
        Bounds target = items.front().bounds;
        if (found != items.end()) {
            target = found->bounds;
        } else if (p.contains("referenceId") && FindSceneObjectData(document, reference) != nullptr) {
            target = CollectLookDevArrangeItems(*scene, std::span<const SceneObjectId>(&reference, 1u)).front().bounds;
        }
        return moved(MoveLookDevItems(*scene, "Align", items, EDITOR::AlignLookDevItems(items, target, *axis, *anchor)),
                     "Objects aligned.");
    }
    case EDITOR::LookDevArrangeAction::Distribute:
        return moved(MoveLookDevItems(*scene, "Distribute", items, EDITOR::DistributeLookDevItems(items, *axis)),
                     "Objects distributed.");
    case EDITOR::LookDevArrangeAction::Drop: {
        std::vector<MATH::Vec3> moves;
        for (const EDITOR::LookDevArrangeItem& item : items) {
            const MATH::Vec3 center = (item.bounds.min + item.bounds.max) * 0.5f;
            const std::unordered_set<uint64_t> own = CollectObjectSubtreeIds(document, SceneObjectId{item.id});
            const float ground = EDITOR::FindLookDevGroundHeight({center.x, item.bounds.min.y + 0.01f, center.z}, own);
            moves.push_back({0.0f, ground - item.bounds.min.y, 0.0f});
        }
        return moved(MoveLookDevItems(*scene, "Drop To Ground", items, moves),
                     "Objects dropped onto the surface below.");
    }
    case EDITOR::LookDevArrangeAction::Group: {
        const SceneObjectId group =
            GroupLookDevObjects(*scene, roots, p.value("name", TEXT::Localize("ui.lookDev.groupName")));
        if (group.value == 0u) {
            return EDITOR::OperationFailure("GROUP_FAILED", "The objects could not be grouped.");
        }
        return EDITOR::OperationOk("Objects grouped.", {{"groupId", group.value}});
    }
    case EDITOR::LookDevArrangeAction::Mirror:
        return created(MirrorLookDevObjects(*scene, roots, *axis), "Mirrored copies created.");
    case EDITOR::LookDevArrangeAction::Array: {
        EDITOR::LookDevArraySettings settings{};
        const std::optional<EDITOR::LookDevArrayKind> kind =
            ParseNamed(p, "kind", EDITOR::kLookDevArrayKindNames, settings.kind);
        if (!kind) {
            return EDITOR::OperationFailure("INVALID_KIND", "kind is linear, grid or ring.");
        }
        settings.kind = *kind;
        settings.count = std::clamp(p.value("count", settings.count), 2, 24);
        settings.rows = std::clamp(p.value("rows", settings.rows), 1, 24);
        settings.spacing = p.value("spacing", settings.spacing);
        settings.radius = p.value("radius", settings.radius);
        if (!MATH::IsFinite(settings.spacing) || !MATH::IsFinite(settings.radius)) {
            return EDITOR::OperationFailure("INVALID_DISTANCE", "spacing and radius must be finite.");
        }
        settings.spacing = std::clamp(settings.spacing, 0.05f, 500.0f);
        settings.radius = std::clamp(settings.radius, 0.05f, 500.0f);
        return created(CreateLookDevArray(*scene, roots, settings), "Array created.");
    }
    }
    return EDITOR::OperationFailure("INVALID_ACTION", "Unknown action.");
}

EDITOR::EditorOperationResult DocumentSceneEditorController::ReplaceLookDevShapeWithModel(const nlohmann::json& p) {
    DocumentSceneBase* scene = aiOperationScene_;
    if (scene == nullptr || scene->IsRuntimePlayActive()) {
        return EDITOR::OperationFailure("EDIT_MODE_REQUIRED", "Replace objects outside Play mode.");
    }
    const SceneObjectId objectId{p.at("objectId").get<uint64_t>()};
    const std::string modelGuid = p.at("modelId").get<std::string>();
    ModelAsset* model = ResolveModelAssetNow(scene->GetModelManager(), scene->GetAssetRegistry(), modelGuid);
    if (model == nullptr || !BOUNDS::IsUsable(model->bounds)) {
        return EDITOR::OperationFailure("MODEL_UNAVAILABLE", TEXT::Localize("ui.lookDev.replaceFailed"));
    }
    const Bounds modelBounds = model->bounds;
    float fitScale = 0.0f;
    const bool replaced = EditLookDevDocument(*scene, "Replace With Model", [&](SceneDocument& document) {
        SceneObjectData* object = FindSceneObjectData(document, objectId);
        SceneComponentData* shape =
            object != nullptr ? EDITOR::FindLookDevComponent(*object, "ProceduralMeshComponent") : nullptr;
        SceneComponentData* modelComponent =
            object != nullptr ? EDITOR::FindLookDevComponent(*object, "ModelComponent") : nullptr;
        if (shape == nullptr || modelComponent == nullptr || EDITOR::IsObjectEditorLocked(document, objectId)) {
            return false;
        }
        ProceduralMeshComponent probe;
        probe.Deserialize(shape->properties);
        const MATH::Vec3 objectScale = object->transform.scale;
        const MATH::Vec3 local = GetProceduralMeshExtent(probe.GetSettings());
        const MATH::Vec3 box{local.x * objectScale.x, local.y * objectScale.y, local.z * objectScale.z};
        const MATH::Vec3 modelSize = modelBounds.max - modelBounds.min;
        // 比率を保ったまま白模の箱に収まる倍率。
        float fit = 0.0f;
        for (int axis = 0; axis < 3; ++axis) {
            const float boxSize = EDITOR::LookDevAxis(box, axis);
            const float size = EDITOR::LookDevAxis(modelSize, axis);
            if (boxSize > 1.0e-4f && size > 1.0e-4f) {
                fit = fit > 0.0f ? (std::min)(fit, boxSize / size) : boxSize / size;
            }
        }
        if (!(fit > 0.0f)) {
            return false;
        }
        // 模型の底の中心を白模の底の中心へ合わせる（向きは白模のまま）。
        const MATH::Vec3& euler = object->transform.rotationEulerDeg;
        const MATH::Vec3 boxBottom = object->transform.position + Rotate(euler, {0.0f, -box.y * 0.5f, 0.0f});
        const MATH::Vec3 modelBottom{(modelBounds.min.x + modelBounds.max.x) * 0.5f, modelBounds.min.y,
                                     (modelBounds.min.z + modelBounds.max.z) * 0.5f};
        object->transform.position = boxBottom - Rotate(euler, modelBottom * fit);
        object->transform.scale = {fit, fit, fit};
        modelComponent->properties["assetId"] = modelGuid;
        object->components.erase(object->components.begin() + (shape - object->components.data()));
        fitScale = fit;
        return true;
    });
    if (!replaced) {
        return EDITOR::OperationFailure("NOT_A_SHAPE", "The object is not an unlocked procedural shape.");
    }
    return EDITOR::OperationOk("Shape replaced with the model.", {{"objectId", objectId.value}, {"scale", fitScale}});
}

void DocumentSceneEditorController::DrawLookDevArrangeSection(DocumentSceneBase& scene) {
    if (!ImGui::CollapsingHeader(TEXT::Localize("ui.lookDev.arrange").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    const size_t selected =
        CollectRootObjectIds(scene.GetSceneDocument(), context_.selection.GetSelectedObjectIds()).size();
    if (selected == 0u) {
        ImGui::TextDisabled("%s", TEXT::Localize("ui.lookDev.arrangeHint").c_str());
        return;
    }
    // ボタンは scene.arrange を選んだ物に対して呼ぶ（智能体と同じ操作）。
    const nlohmann::json selection = IdList(context_.selection.GetSelectedObjectIds());
    const auto arrange = [&](nlohmann::json parameters) {
        parameters["objectIds"] = selection;
        SelectLookDevOperationResult(scene, RunLookDevOperation("scene.arrange", parameters));
    };
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float labelWidth = ImGui::CalcTextSize("XX").x;
    const auto buttonWidth = [&](int count) {
        return (ImGui::GetContentRegionAvail().x - labelWidth - spacing * static_cast<float>(count)) /
               static_cast<float>(count);
    };
    // 揃え：基準は主に選んでいる物（检查器に出ている物）。
    ImGui::BeginDisabled(selected < 2u);
    ImGui::TextDisabled("%s", TEXT::Localize("ui.lookDev.align").c_str());
    constexpr const char* kAxisNames[] = {"X", "Y", "Z"};
    constexpr const char* kAnchorKeys[] = {"ui.lookDev.alignMin", "ui.lookDev.alignCenter", "ui.lookDev.alignMax"};
    for (int axis = 0; axis < 3; ++axis) {
        ImGui::PushID(axis);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(kAxisNames[axis]);
        ImGui::SameLine(0.0f, labelWidth - ImGui::CalcTextSize(kAxisNames[axis]).x + spacing);
        const float width = buttonWidth(3);
        for (int anchor = 0; anchor < 3; ++anchor) {
            if (anchor > 0) {
                ImGui::SameLine();
            }
            ImGui::PushID(anchor);
            if (EDITOR::ActionButton(TEXT::Localize(kAnchorKeys[anchor]).c_str(), "##align",
                                     EDITOR::EditorButtonTone::Neutral, ImVec2(width, 0.0f))) {
                arrange({{"action", "align"},
                         {"axis", EDITOR::kLookDevAxisNames[axis].name},
                         {"anchor", EDITOR::kLookDevAlignNames[anchor].name},
                         {"referenceId", context_.selection.GetActiveObjectId().value}});
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(selected < 3u);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", TEXT::Localize("ui.lookDev.distribute").c_str());
    for (int axis = 0; axis < 3; ++axis) {
        ImGui::SameLine();
        ImGui::PushID(10 + axis);
        if (EDITOR::ActionButton(kAxisNames[axis], "##distribute", EDITOR::EditorButtonTone::Neutral)) {
            arrange({{"action", "distribute"}, {"axis", EDITOR::kLookDevAxisNames[axis].name}});
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();

    const float half = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
    if (EDITOR::ActionButton(TEXT::Localize("ui.lookDev.dropToGround").c_str(), "##drop",
                             EDITOR::EditorButtonTone::Neutral, ImVec2(half, 0.0f),
                             TEXT::Localize("ui.lookDev.dropToGroundHint").c_str())) {
        arrange({{"action", "drop"}});
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(selected < 2u);
    if (EDITOR::ActionButton(TEXT::Localize("ui.lookDev.group").c_str(), "##group", EDITOR::EditorButtonTone::Neutral,
                             ImVec2(half, 0.0f), TEXT::Localize("ui.lookDev.groupHint").c_str())) {
        arrange({{"action", "group"}});
    }
    ImGui::EndDisabled();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", TEXT::Localize("ui.lookDev.mirror").c_str());
    for (const int axis : {0, 2}) {
        ImGui::SameLine();
        ImGui::PushID(20 + axis);
        if (EDITOR::ActionButton(kAxisNames[axis], "##mirror", EDITOR::EditorButtonTone::Neutral, ImVec2(0.0f, 0.0f),
                                 TEXT::Localize("ui.lookDev.mirrorHint").c_str())) {
            arrange({{"action", "mirror"}, {"axis", EDITOR::kLookDevAxisNames[axis].name}});
        }
        ImGui::PopID();
    }

    // 阵列：線形・格子・環形。
    EDITOR::LookDevArraySettings& array = lookDevWorkspace_.ArraySettings();
    ImGui::SeparatorText(TEXT::Localize("ui.lookDev.array").c_str());
    for (const auto& entry : EDITOR::kLookDevArrayKindNames) {
        if (entry.value != EDITOR::LookDevArrayKind::Linear) {
            ImGui::SameLine();
        }
        if (EDITOR::ToggleButton(TEXT::Localize(std::string("ui.lookDev.arrayKind.") + entry.name).c_str(), entry.name,
                                 array.kind == entry.value)) {
            array.kind = entry.value;
        }
    }
    if (EDITOR::InspectorPropertyRow row(TEXT::Localize("ui.lookDev.arrayCount")); row.IsVisible()) {
        (void)EDITOR::SliderNumber("##count", array.count, 2.0, 24.0);
    }
    if (array.kind == EDITOR::LookDevArrayKind::Grid) {
        if (EDITOR::InspectorPropertyRow row(TEXT::Localize("ui.lookDev.arrayRows")); row.IsVisible()) {
            (void)EDITOR::SliderNumber("##rows", array.rows, 1.0, 24.0);
        }
    }
    if (EDITOR::InspectorPropertyRow row(TEXT::Localize(
            array.kind == EDITOR::LookDevArrayKind::Ring ? "ui.lookDev.arrayRadius" : "ui.lookDev.arraySpacing"));
        row.IsVisible()) {
        float& distance = array.kind == EDITOR::LookDevArrayKind::Ring ? array.radius : array.spacing;
        (void)EDITOR::DragNumber("##distance", distance, {.minimum = 0.05, .maximum = 500.0});
    }
    if (EDITOR::ActionButton(TEXT::Localize("ui.lookDev.arrayCreate").c_str(), "##arrayCreate",
                             EDITOR::EditorButtonTone::Primary, ImVec2(-1.0f, 0.0f),
                             TEXT::Localize("ui.lookDev.arrayHint").c_str())) {
        arrange({{"action", "array"},
                 {"kind", TEXT::EnumToName(EDITOR::kLookDevArrayKindNames, array.kind)},
                 {"count", array.count},
                 {"rows", array.rows},
                 {"spacing", array.spacing},
                 {"radius", array.radius}});
    }

    DrawLookDevOpeningsSection(scene);

    // 换成模型：白模の箱を選んだ模型に置き換え、箱に収まる大きさで底を合わせる。
    const SceneObjectId active = context_.selection.GetActiveObjectId();
    const GameObject* activeObject = selected == 1u ? scene.GetWorld().FindObject(active) : nullptr;
    if (activeObject == nullptr || activeObject->GetComponent<ProceduralMeshComponent>() == nullptr) {
        return;
    }
    ImGui::SeparatorText(TEXT::Localize("ui.lookDev.replace").c_str());
    std::string& model = lookDevWorkspace_.ReplacementModel();
    const AssetDatabase& database = scene.GetAssetDatabase();
    const std::vector<const AssetRecord*> models = database.CollectByType(AssetType::Model);
    const auto current = std::find_if(models.begin(), models.end(),
                                      [&](const AssetRecord* record) { return record->guid.value == model; });
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##replaceModel", current != models.end()
                                                ? GetAssetRecordDisplayName(**current).c_str()
                                                : TEXT::Localize("ui.lookDev.replacePick").c_str())) {
        for (const AssetRecord* record : models) {
            if (ImGui::Selectable((GetAssetRecordDisplayName(*record) + "##" + record->guid.value).c_str(),
                                  record->guid.value == model)) {
                model = record->guid.value;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(current == models.end());
    if (EDITOR::ActionButton(TEXT::Localize("ui.lookDev.replaceApply").c_str(), "##replaceApply",
                             EDITOR::EditorButtonTone::Primary, ImVec2(-1.0f, 0.0f),
                             TEXT::Localize("ui.lookDev.replaceHint").c_str())) {
        RunLookDevOperation("scene.replace_model", {{"objectId", active.value}, {"modelId", model}});
    }
    ImGui::EndDisabled();
}

} // namespace HIKARI
