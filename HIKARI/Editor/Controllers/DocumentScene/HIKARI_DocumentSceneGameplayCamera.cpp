#include "Assets/Camera/HIKARI_GameplayCameraAsset.h"
#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"
#include "Editor/HIKARI_DocumentComponentAuthoringService.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Core/HIKARI_JsonRead.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"
#include <algorithm>

namespace HIKARI {
EDITOR::EditorOperationResult DocumentSceneEditorController::BindGameplayCamera(const nlohmann::json& p) {
    auto* scene = aiOperationScene_;
    if (!scene || scene->IsRuntimePlayActive())
        return {false, "EDIT_MODE_REQUIRED", "Bind a camera outside Play mode."};
    const AssetGuid guid{p.at("assetGuid").get<std::string>()};
    const auto* record = scene->GetAssetDatabase().FindByGuid(guid);
    if (!record || record->type != AssetType::GameplayCamera)
        return {false, "ASSET_NOT_FOUND", "Save a gameplay camera profile first."};
    CAMERA::GameplayCameraProfile profile;
    std::string error;
    const auto path = scene->GetAssetDatabase().ResolveSourcePath(*record);
    if (!LoadGameplayCameraAsset(path, profile, error))
        return {false, "INVALID_CAMERA_PROFILE", error};
    const SceneObjectId target{p.at("targetId").get<uint64_t>()};
    const SceneObjectId look{p.value("lookTargetId", uint64_t{0})};
    if (!scene->GetWorld().FindObject(target) || (look.value && !scene->GetWorld().FindObject(look)))
        return {false, "TARGET_NOT_FOUND", "Select existing follow and look targets."};
    auto& document = scene->GetSceneDocument();
    const SceneObjectId requested{p.value("cameraId", uint64_t{0})};
    auto found = std::find_if(document.objects.begin(), document.objects.end(),
                              [&](const auto& object) { return object.id == requested; });
    if (requested.value && found == document.objects.end())
        return {false, "CAMERA_NOT_FOUND", "Selected camera no longer exists."};
    const bool create = found == document.objects.end();
    SceneObjectData object = create ? SceneObjectData{} : *found;
    if (create) {
        object.id = AllocateSceneObjectId(document);
        object.name = p.value("name", profile.name);
        // 固定・軌道などの機位は置いた位置が意味を持つ。新しく作る時だけ位置と向きを受け取る。
        object.transform.position = JSONREAD::Vec3MemberOr(p, "position", object.transform.position);
        object.transform.rotationEulerDeg = JSONREAD::Vec3MemberOr(p, "rotation", object.transform.rotationEulerDeg);
    }
    if (object.id == target)
        return {false, "INVALID_TARGET", "A camera cannot follow itself."};
    DocumentComponentAuthoringService service;
    const auto added = service.AddComponent(scene->GetComponentRegistry(), object, "CameraFollowComponent");
    auto follow = std::find_if(object.components.begin(), object.components.end(),
                               [](const auto& c) { return c.type == "CameraFollowComponent"; });
    if (follow == object.components.end())
        return {false, "COMPONENT_FAILED", added.message};
    const auto mode = p.value("mode", profile.defaultMode);
    if (!profile.FindMode(mode))
        return {false, "MODE_NOT_FOUND", "Selected mode does not exist."};
    auto& props = follow->properties;
    props["enabled"] = true;
    props["targetObjectId"] = SCENE::SERIALIZATION::WriteSceneObjectReference(target.value);
    props["cameraProfile"] = guid.value;
    props["cameraMode"] = mode;
    props["cameraLookTarget"] = SCENE::SERIALIZATION::WriteSceneObjectReference(look.value);
    if (p.value("resetOverrides", false))
        props["cameraOverrides"] = nlohmann::json::object();
    NormalizeSceneComponentIds(object);
    const auto id = object.id;
    auto beforeObjects = document.objects;
    const auto beforeCamera = document.camera;
    const bool dirty = context_.sceneDirty || scene->HasUnsavedSceneChanges();
    if (create)
        document.objects.push_back(std::move(object));
    else
        *found = std::move(object);
    if (p.value("setDefault", true))
        document.camera.defaultCameraObjectId = id;
    CommitSceneObjectAuthoringHistory(*scene, {"Bind gameplay camera", std::move(beforeObjects), document.objects,
                                               beforeCamera, document.camera, dirty, true, true});
    return {true, "OK", "Gameplay camera bound", {{"cameraId", id.value}, {"assetGuid", guid.value}}};
}

EDITOR::EditorOperationResult DocumentSceneEditorController::EditCameraZone(const nlohmann::json& p) {
    auto* scene = aiOperationScene_;
    if (!scene || scene->IsRuntimePlayActive())
        return {false, "EDIT_MODE_REQUIRED", "Edit camera zones outside Play mode."};
    auto& document = scene->GetSceneDocument();
    const std::string action = p.value("action", std::string("create"));
    const SceneObjectId requested{p.value("objectId", uint64_t{0})};
    auto found = std::find_if(document.objects.begin(), document.objects.end(),
                              [&](const auto& object) { return object.id == requested; });
    if (action != "create" && found == document.objects.end())
        return {false, "ZONE_NOT_FOUND", "The camera zone no longer exists."};
    auto beforeObjects = document.objects;
    const auto beforeCamera = document.camera;
    const bool dirty = context_.sceneDirty || scene->HasUnsavedSceneChanges();
    const auto commit = [&](const char* label) {
        CommitSceneObjectAuthoringHistory(*scene,
                                          {label, std::move(beforeObjects), document.objects, beforeCamera,
                                           document.camera, dirty, true, true, p.value("mergeKey", uint64_t{0})});
    };
    if (action == "remove") {
        document.objects.erase(found);
        commit("Remove camera zone");
        return {true, "OK", "Camera zone removed"};
    }
    const bool create = action == "create";
    SceneObjectData object = create ? SceneObjectData{} : *found;
    if (create) {
        object.id = AllocateSceneObjectId(document);
        object.name = "Camera Zone";
        DocumentComponentAuthoringService service;
        const auto added =
            service.AddComponent(scene->GetComponentRegistry(), object, "CameraActivationVolumeComponent");
        if (!added.success)
            return {false, "COMPONENT_FAILED", added.message};
    }
    auto zone = std::find_if(object.components.begin(), object.components.end(),
                             [](const auto& c) { return c.type == "CameraActivationVolumeComponent"; });
    if (zone == object.components.end())
        return {false, "NOT_A_ZONE", "The object is not a camera zone."};
    object.name = p.value("name", object.name);
    object.transform.position = JSONREAD::Vec3MemberOr(p, "position", object.transform.position);
    auto& props = zone->properties;
    if (p.contains("cameraId"))
        props["cameraObjectId"] = SCENE::SERIALIZATION::WriteSceneObjectReference(p.at("cameraId").get<uint64_t>());
    if (p.contains("subjectId"))
        props["subjectObjectId"] = SCENE::SERIALIZATION::WriteSceneObjectReference(p.at("subjectId").get<uint64_t>());
    if (p.contains("halfExtents")) {
        const auto extents = JSONREAD::Vec3MemberOr(p, "halfExtents", {4.0f, 3.0f, 4.0f});
        props["halfExtents"] = {extents.x, extents.y, extents.z};
    }
    for (const char* key : {"priority", "blendSeconds", "affectsControlBasis", "enabled"})
        if (p.contains(key))
            props[key] = p.at(key);
    NormalizeSceneComponentIds(object);
    const auto id = object.id;
    if (create)
        document.objects.push_back(std::move(object));
    else
        *found = std::move(object);
    commit(create ? "Create camera zone" : "Edit camera zone");
    return {true, "OK", "Camera zone saved", {{"objectId", id.value}}};
}
} // namespace HIKARI
