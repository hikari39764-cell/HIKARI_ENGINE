#include "Editor/Authoring/HIKARI_SceneObjectOperations.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include "Editor/Authoring/HIKARI_GroundPlacement.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/HIKARI_World.h"

// 摆放の操作：地面へ置く、Play の結果を残す。人の命令（End・編集メニュー）と智能体が同じ道を通る。
namespace HIKARI::EDITOR {
namespace {
// 対象：objectIds（安定した SceneObjectId の配列）、無ければ人の選択。
bool ReadTargetObjectIds(const SceneObjectOperationContext& operation, const nlohmann::json& parameters,
                         std::vector<SceneObjectId>& targets, EditorOperationResult& failure) {
    if (const auto ids = parameters.find("objectIds"); ids != parameters.end()) {
        if (!ids->is_array() ||
            !std::all_of(ids->begin(), ids->end(), [](const nlohmann::json& id) { return id.is_number_unsigned(); })) {
            failure = OperationFailure("INVALID_ARGUMENT", "objectIds must be an array of SceneObjectIds.");
            return false;
        }
        for (const auto& id : *ids) {
            targets.push_back(SceneObjectId{id.get<uint64_t>()});
        }
    } else {
        targets = operation.editor.selection.GetSelectedObjectIds();
    }
    if (targets.empty()) {
        failure = OperationFailure("NO_TARGET", "Select objects, or pass objectIds.");
        return false;
    }
    return true;
}

} // namespace

// 物体の模型の底を真下の地面へ置き、坂に合わせて傾ける（Unreal の End キーと同じ）。向きは保つ。
// 置き方は物体の GroundAnchorComponent、無ければ既定。引数で個別に上書きできる（GroundAnchorComponent を持つ物は
// 記録の時にその置き方で置き直される）。親を持つ物と固定した物は動かさない。
EditorOperationResult DropSceneObjectsToGround(SceneObjectOperationContext& operation,
                                               const nlohmann::json& parameters) {
    DocumentSceneBase& scene = operation.scene;
    if (scene.IsRuntimePlayActive()) {
        return OperationFailure("PLAY_ACTIVE", "Stop Play before moving objects.");
    }
    std::vector<SceneObjectId> targets{};
    EditorOperationResult failure{};
    if (!ReadTargetObjectIds(operation, parameters, targets, failure)) {
        return failure;
    }
    SceneDocument& document = scene.GetSceneDocument();
    const bool dirty = operation.editor.sceneDirty || scene.HasUnsavedSceneChanges();
    std::vector<SceneObjectData> before = document.objects;
    nlohmann::json dropped = nlohmann::json::array();
    nlohmann::json skipped = nlohmann::json::array();
    const std::unordered_map<uint64_t, ModelFootprint> footprints = CollectModelFootprints();
    for (const SceneObjectId id : targets) {
        SceneObjectData* object = FindSceneObjectData(document, id);
        if (object == nullptr || object->parent.has_value() || object->editorLocked) {
            skipped.push_back(id.value);
            continue;
        }
        GroundPlacementSettings settings = ResolveGroundPlacement(*object);
        settings.alignToGround = parameters.value("alignToGround", settings.alignToGround);
        settings.maxTiltDegrees = parameters.value("maxTiltDegrees", settings.maxTiltDegrees);
        settings.sinkMeters = parameters.value("sinkMeters", settings.sinkMeters);
        TransformData placed{};
        const auto footprint = footprints.find(id.value);
        if (!PlaceOnGround(scene.GetWorld(), object->transform,
                           footprint != footprints.end() ? &footprint->second : nullptr, settings, placed)) {
            skipped.push_back(id.value);
            continue;
        }
        object->transform = placed;
        dropped.push_back(id.value);
    }
    if (dropped.empty()) {
        return OperationFailure("NO_GROUND",
                                "No ground below the objects (objects with a parent or locked are not moved).");
    }
    if (before == document.objects) {
        return OperationOk("Already on the ground.", {{"objectIds", dropped}, {"skipped", skipped}});
    }
    operation.history = SceneObjectAuthoringHistoryRequest{
        "Drop To Ground", std::move(before), document.objects, document.camera, document.camera, dirty, true, true, 0u};
    return OperationOk("Dropped " + std::to_string(dropped.size()) + " object(s) to the ground.",
                       {{"objectIds", dropped}, {"skipped", skipped}});
}

// Play 中に物理などで動いた物の今の姿勢を記録し、Play を止めて文書を戻した後に書き戻す（Unreal の「シミュレーションの
// 変更を保持」）。物理で落として岩を自然に積む用途。親を持つ物は親に従うので記録しない。
EditorOperationResult KeepPlayResults(SceneObjectOperationContext& operation, const nlohmann::json& parameters) {
    DocumentSceneBase& scene = operation.scene;
    if (!scene.IsRuntimePlayActive()) {
        return OperationFailure("PLAY_INACTIVE", "Start Play, let the objects settle, then keep their results.");
    }
    std::vector<SceneObjectId> targets{};
    EditorOperationResult failure{};
    if (!ReadTargetObjectIds(operation, parameters, targets, failure)) {
        return failure;
    }
    std::vector<KeptPlayPose>& kept = operation.editor.keptPlayPoses;
    if (operation.editor.keptPlayScenePath != scene.GetScenePath()) {
        kept.clear();
        operation.editor.keptPlayScenePath = scene.GetScenePath();
    }
    nlohmann::json recorded = nlohmann::json::array();
    nlohmann::json skipped = nlohmann::json::array();
    for (const SceneObjectId id : targets) {
        const GameObject* object = scene.GetWorld().FindObject(id);
        if (object == nullptr || object->GetParent() != nullptr) {
            skipped.push_back(id.value);
            continue;
        }
        const Transform3D& transform = object->GetTransform();
        const KeptPlayPose pose{id, transform.position, MATH::EulerXYZDegreesFromQuat(transform.rotation),
                                transform.scale};
        const auto existing =
            std::find_if(kept.begin(), kept.end(), [id](const KeptPlayPose& entry) { return entry.id == id; });
        if (existing != kept.end()) {
            *existing = pose;
        } else {
            kept.push_back(pose);
        }
        recorded.push_back(id.value);
    }
    if (recorded.empty()) {
        return OperationFailure("NO_TARGET", "None of the objects can be kept (objects with a parent follow it).");
    }
    return OperationOk("Kept " + std::to_string(recorded.size()) +
                           " object(s). Their poses are written into the scene when Play stops.",
                       {{"objectIds", recorded}, {"skipped", skipped}});
}

} // namespace HIKARI::EDITOR
