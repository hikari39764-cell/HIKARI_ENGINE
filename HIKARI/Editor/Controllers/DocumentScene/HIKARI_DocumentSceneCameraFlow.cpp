#include "Assets/Camera/HIKARI_GameplayCameraAsset.h"
#include "Core/HIKARI_JsonRead.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"
#include "Editor/HIKARI_DocumentComponentAuthoringService.h"
#include "Scene/Components/HIKARI_CameraFlowComponent.h"
#include "Scene/Components/HIKARI_CameraFollowComponent.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include <algorithm>
namespace HIKARI {
EDITOR::EditorOperationResult DocumentSceneEditorController::EditCameraFlow(const nlohmann::json& p) {
    auto* scene = aiOperationScene_;
    if (!scene || scene->IsRuntimePlayActive())
        return {false, "EDIT_MODE_REQUIRED", "Edit camera flows outside Play."};
    const auto action = p.value("action", std::string{"update"});
    if (action != "create" && action != "update" && action != "remove")
        return {false, "INVALID_ACTION", "Use create, update, or remove."};
    auto& document = scene->GetSceneDocument();
    auto objects = document.objects;
    const SceneObjectId id{p.value("objectId", uint64_t{0})};
    auto found = std::find_if(objects.begin(), objects.end(), [&](const auto& o) { return o.id == id; });
    if (action != "create" && found == objects.end())
        return {false, "NOT_FOUND", "Flow object not found."};
    if (action == "create") {
        SceneObjectData object;
        object.id = AllocateSceneObjectId(document);
        object.name = "Camera Flow";
        DocumentComponentAuthoringService authoring;
        const auto added = authoring.AddComponent(scene->GetComponentRegistry(), object, "CameraFlowComponent");
        if (!added.success)
            return {false, "COMPONENT_UNAVAILABLE", added.message};
        objects.push_back(std::move(object));
        found = std::prev(objects.end());
    }
    auto component = std::find_if(found->components.begin(), found->components.end(),
                                  [](const auto& c) { return c.type == "CameraFlowComponent"; });
    if (component == found->components.end())
        return {false, "NOT_A_FLOW", "Object has no CameraFlowComponent."};
    const auto resultId = found->id;
    if (action == "remove") {
        // フローだけを除去し、同じオブジェクト上の他のコンポーネントは保持する。
        found->components.erase(component);
    } else {
        auto props = component->properties;
        if (p.contains("settings"))
            props.merge_patch(p.at("settings"));
        CameraFlowComponent typed;
        std::string error;
        if (!typed.TryDeserialize(props, error))
            return {false, "INVALID_SETTINGS", error};
        const auto& settings = typed.Settings();
        const auto* camera = scene->GetWorld().FindObject(settings.cameraObjectId);
        const auto* follow = camera ? camera->GetComponent<CameraFollowComponent>() : nullptr;
        if (!scene->GetWorld().FindObject(settings.subjectObjectId) || !follow)
            return {false, "INVALID_TARGET", "Choose an existing subject and a bound gameplay camera."};
        const auto* sequence = scene->GetAssetDatabase().FindByGuid(settings.sequence);
        if (settings.sequence.IsValid() && (!sequence || sequence->type != AssetType::Sequence))
            return {false, "SEQUENCE_REQUIRED", "Choose a sequence asset."};
        GameplayCameraAssetStore store;
        store.Configure(&scene->GetAssetDatabase());
        const auto profile = store.Load(follow->GameplayBinding().profile, error);
        if (!profile || (!settings.afterMode.empty() && !profile->FindMode(settings.afterMode)))
            return {false, "MODE_NOT_FOUND", "Choose a destination mode in this camera's saved profile."};
        if (!settings.requiredMode.empty() && !profile->FindMode(settings.requiredMode))
            return {false, "MODE_NOT_FOUND", "Required mode is missing from the camera profile."};
        typed.Serialize(component->properties);
        if (p.contains("name"))
            found->name = p.at("name").get<std::string>();
        if (p.contains("position"))
            found->transform.position = JSONREAD::Vec3Or(p.at("position"), found->transform.position);
        if (p.contains("rotation"))
            found->transform.rotationEulerDeg = JSONREAD::Vec3Or(p.at("rotation"), found->transform.rotationEulerDeg);
        if (!MATH::IsFinite(found->transform.position) || !MATH::IsFinite(found->transform.rotationEulerDeg))
            return {false, "INVALID_TRANSFORM", "Position and rotation must be finite."};
        NormalizeSceneComponentIds(*found);
    }
    auto before = document.objects;
    const bool dirty = context_.sceneDirty || scene->HasUnsavedSceneChanges();
    document.objects = std::move(objects);
    CommitSceneObjectAuthoringHistory(*scene, {"Edit camera flow", std::move(before), document.objects, document.camera,
                                               document.camera, dirty, true, true, p.value("mergeKey", uint64_t{0})});
    return {true, "OK", "Camera flow updated", {{"objectId", resultId.value}}};
}
} // namespace HIKARI
