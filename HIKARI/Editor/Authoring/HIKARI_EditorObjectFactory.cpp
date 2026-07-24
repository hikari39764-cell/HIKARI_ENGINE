#include "HIKARI_EditorObjectFactory.h"

#include <algorithm>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

    namespace {
        SceneObjectId AllocateSceneObjectId(const SceneDocument& document) {
            uint64_t maxId = 0;
            for (const SceneObjectData& object : document.objects) {
                maxId = (std::max)(maxId, object.id.value);
            }
            return SceneObjectId{ maxId + 1 };
        }

        GameObject* FindRuntimeObject(DocumentSceneBase& scene, SceneObjectId id) {
            return scene.GetWorld().FindObject(id);
        }

        std::string ResolveObjectName(
            const DocumentSceneBase& scene,
            const AssetGuid* modelGuid,
            const CreateObjectRequest& request,
            SceneObjectId id) {

            if (!request.name.empty()) {
                return request.name;
            }
            if (modelGuid && modelGuid->IsValid()) {
                if (const AssetRecord* record = scene.GetAssetDatabase().FindByGuid(*modelGuid)) {
                    if (!record->displayName.empty()) {
                        return record->displayName;
                    }
                    if (!record->sourcePath.empty()) {
                        return record->sourcePath.stem().string();
                    }
                }
            }
            return "GameObject_" + std::to_string(id.value);
        }

        SceneObjectData BuildBaseObject(
            DocumentSceneBase& scene,
            const AssetGuid* modelGuid,
            const CreateObjectRequest& request) {

            SceneObjectData object{};
            object.id = AllocateSceneObjectId(scene.GetSceneDocument());
            object.name = ResolveObjectName(scene, modelGuid, request, object.id);
            object.transform.position = request.position;
            object.transform.rotationEulerDeg = request.rotation;
            object.transform.scale = request.scale;
            return object;
        }
    }

    GameObject* CreateEmptyObject(
        DocumentSceneBase& scene,
        const CreateObjectRequest& request) {

        SceneObjectData object = BuildBaseObject(scene, nullptr, request);
        const SceneObjectId id = object.id;
        scene.GetSceneDocument().objects.push_back(std::move(object));

        // Document を正として runtime world を作り直す。
        scene.RebuildRuntimeWorld();
        return FindRuntimeObject(scene, id);
    }

    GameObject* CreateModelObject(
        DocumentSceneBase& scene,
        const AssetGuid& modelGuid,
        const CreateObjectRequest& request) {

        SceneObjectData object = BuildBaseObject(scene, &modelGuid, request);
        const SceneObjectId id = object.id;

        SceneComponentData modelComponent{};
        modelComponent.type = "ModelComponent";
        modelComponent.properties = {
            { "assetId", modelGuid.value },
            { "visible", true },
            { "castShadow", true },
            { "receiveShadow", true }
        };
        object.components.push_back(std::move(modelComponent));

        scene.GetSceneDocument().objects.push_back(std::move(object));

        // Document を正として runtime world を作り直す。
        scene.RebuildRuntimeWorld();
        return FindRuntimeObject(scene, id);
    }

    GameObject* CreatePrimitiveObject(
        DocumentSceneBase& scene,
        const CreatePrimitiveRequest& request) {

        CreateObjectRequest objectRequest = request.object;
        const ProceduralMeshSettings settings =
            SanitizeProceduralMeshSettings(request.mesh);
        if (objectRequest.name.empty()) {
            objectRequest.name =
                GetProceduralMeshDisplayName(settings.kind);
        }

        SceneObjectData object = BuildBaseObject(
            scene, nullptr, objectRequest);
        const SceneObjectId id = object.id;

        object.components.push_back(SceneComponentData{
            "ModelComponent",
            {
                { "assetId", "" },
                { "visible", true },
                { "castShadow", true },
                { "receiveShadow", true },
                { "renderStatic", false }
            }
        });
        object.components.push_back(SceneComponentData{
            "ProceduralMeshComponent",
            {
                { "kind", ToString(settings.kind) },
                { "width", settings.width },
                { "height", settings.height },
                { "depth", settings.depth },
                { "segmentsX", settings.segmentsX },
                { "segmentsY", settings.segmentsY },
                { "segmentsZ", settings.segmentsZ },
                { "sphereSlices", settings.sphereSlices },
                { "sphereStacks", settings.sphereStacks },
                { "doubleSided", settings.doubleSided },
                { "generateTangents", settings.generateTangents }
            }
        });

        if (request.addCollider) {
            nlohmann::json collider{
                { "enabled", true },
                { "fitMode", "Geometry" },
                { "center", { 0.0f, 0.0f, 0.0f } },
                { "rotation", { 0.0f, 0.0f, 0.0f } },
                { "trigger", false },
                { "material", {
                    { "friction", 0.5f },
                    { "restitution", 0.0f },
                    { "density", 1.0f }
                } },
                { "filter", {
                    { "layer", 1u },
                    { "mask", 0xFFFFFFFFu }
                } }
            };
            switch (settings.kind) {
            case ProceduralMeshKind::Sphere:
                collider["shape"] = "Sphere";
                collider["radius"] = settings.width * 0.5f;
                collider["height"] = settings.width;
                collider["size"] = {
                    settings.width, settings.width, settings.width
                };
                break;
            case ProceduralMeshKind::Capsule:
            case ProceduralMeshKind::Cylinder:
                // The backend contract currently exposes capsule as the
                // closest stable collider for round vertical primitives.
                collider["shape"] = "Capsule";
                collider["radius"] = settings.width * 0.5f;
                collider["height"] = settings.height;
                collider["size"] = {
                    settings.width, settings.height, settings.width
                };
                break;
            case ProceduralMeshKind::Plane:
            case ProceduralMeshKind::GridPlane:
                collider["shape"] = "Box";
                collider["size"] = {
                    settings.width, 0.02f, settings.depth
                };
                collider["radius"] = 0.01f;
                collider["height"] = 0.02f;
                break;
            case ProceduralMeshKind::Box:
            default:
                collider["shape"] = "Box";
                collider["size"] = {
                    settings.width, settings.height, settings.depth
                };
                collider["radius"] =
                    (std::min)({ settings.width, settings.height, settings.depth }) * 0.5f;
                collider["height"] = settings.height;
                break;
            }
            object.components.push_back(SceneComponentData{
                "ColliderComponent",
                std::move(collider)
            });
        }

        scene.GetSceneDocument().objects.push_back(std::move(object));
        scene.RebuildRuntimeWorld();
        return FindRuntimeObject(scene, id);
    }

} // namespace HIKARI::EDITOR
