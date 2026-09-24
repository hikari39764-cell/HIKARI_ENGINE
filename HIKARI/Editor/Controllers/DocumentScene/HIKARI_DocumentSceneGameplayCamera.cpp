#include "Assets/Camera/HIKARI_GameplayCameraAsset.h"
#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"
#include "Editor/HIKARI_DocumentComponentAuthoringService.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
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
    const auto path = record->sourcePath.is_absolute()
                          ? record->sourcePath
                          : scene->GetAssetDatabase().GetProjectRoot() / record->sourcePath;
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
        object.name = profile.name;
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
    document.camera.defaultCameraObjectId = id;
    CommitSceneObjectAuthoringHistory(*scene, {"Bind gameplay camera", std::move(beforeObjects), document.objects,
                                               beforeCamera, document.camera, dirty, true, true});
    return {true, "OK", "Gameplay camera bound", {{"cameraId", id.value}, {"assetGuid", guid.value}}};
}
} // namespace HIKARI
