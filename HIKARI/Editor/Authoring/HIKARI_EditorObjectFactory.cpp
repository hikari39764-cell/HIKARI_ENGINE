#include "HIKARI_EditorObjectFactory.h"

#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SceneComponentId.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/Components/Rendering/WorldText/HIKARI_WorldTextComponent.h"
#include "Scene/Lighting/HIKARI_LightingAuthoringModel.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"
#include "Scene/Water/HIKARI_WaterSurfaceComponent.h"

namespace HIKARI::EDITOR {

namespace {
GameObject* FindRuntimeObject(DocumentSceneBase& scene, SceneObjectId id) { return scene.GetWorld().FindObject(id); }

std::string ResolveObjectName(const DocumentSceneBase& scene, const AssetGuid* modelGuid,
                              const CreateObjectRequest& request, SceneObjectId id) {
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

SceneObjectData BuildBaseObject(DocumentSceneBase& scene, const AssetGuid* modelGuid,
                                const CreateObjectRequest& request) {
    SceneObjectData object{};
    object.id = AllocateSceneObjectId(scene.GetSceneDocument());
    object.name = ResolveObjectName(scene, modelGuid, request, object.id);
    object.transform.position = request.position;
    object.transform.rotationEulerDeg = request.rotation;
    object.transform.scale = request.scale;
    return object;
}
} // namespace

GameObject* CreateEmptyObject(DocumentSceneBase& scene, const CreateObjectRequest& request) {
    SceneObjectData object = BuildBaseObject(scene, nullptr, request);
    const SceneObjectId id = object.id;
    scene.GetSceneDocument().objects.push_back(std::move(object));

    // Document を正として runtime world を作り直す。
    scene.RebuildRuntimeWorld();
    return FindRuntimeObject(scene, id);
}

GameObject* CreateModelObject(DocumentSceneBase& scene, const AssetGuid& modelGuid,
                              const CreateObjectRequest& request) {
    SceneObjectData object = BuildBaseObject(scene, &modelGuid, request);
    const SceneObjectId id = object.id;

    SceneComponentData modelComponent{};
    modelComponent.type = "ModelComponent";
    modelComponent.properties = {
        {"assetId", modelGuid.value}, {"visible", true}, {"castShadow", true}, {"receiveShadow", true}};
    object.components.push_back(std::move(modelComponent));
    NormalizeSceneComponentIds(object);

    scene.GetSceneDocument().objects.push_back(std::move(object));

    // Document を正として runtime world を作り直す。
    scene.RebuildRuntimeWorld();
    return FindRuntimeObject(scene, id);
}

GameObject* CreatePrimitiveObject(DocumentSceneBase& scene, const CreatePrimitiveRequest& request) {
    CreateObjectRequest objectRequest = request.object;
    const ProceduralMeshSettings settings = SanitizeProceduralMeshSettings(request.mesh);
    if (objectRequest.name.empty()) {
        objectRequest.name = GetProceduralMeshDisplayName(settings.kind);
    }

    SceneObjectData object = BuildBaseObject(scene, nullptr, objectRequest);
    const SceneObjectId id = object.id;

    object.components.push_back(SceneComponentData{
        "ModelComponent",
        {{"assetId", ""}, {"visible", true}, {"castShadow", true}, {"receiveShadow", true}, {"renderStatic", false}}});
    nlohmann::json proceduralProperties{{"kind", ToString(settings.kind)},
                                        {"width", settings.width},
                                        {"height", settings.height},
                                        {"depth", settings.depth},
                                        {"segmentsX", settings.segmentsX},
                                        {"segmentsY", settings.segmentsY},
                                        {"segmentsZ", settings.segmentsZ},
                                        {"sphereSlices", settings.sphereSlices},
                                        {"sphereStacks", settings.sphereStacks},
                                        {"doubleSided", settings.doubleSided},
                                        {"generateTangents", settings.generateTangents},
                                        {"uvTilesX", settings.uvTilesX},
                                        {"uvTilesY", settings.uvTilesY}};
    object.components.push_back(SceneComponentData{"ProceduralMeshComponent", std::move(proceduralProperties)});

    if (request.addCollider) {
        nlohmann::json collider{{"enabled", true},
                                {"fitMode", "Geometry"},
                                {"center", {0.0f, 0.0f, 0.0f}},
                                {"rotation", {0.0f, 0.0f, 0.0f}},
                                {"trigger", false},
                                {"material", {{"friction", 0.5f}, {"restitution", 0.0f}, {"density", 1.0f}}},
                                {"filter", {{"layer", 1u}, {"mask", 0xFFFFFFFFu}}}};
        switch (settings.kind) {
        case ProceduralMeshKind::Sphere:
            collider["shape"] = "Sphere";
            collider["radius"] = settings.width * 0.5f;
            collider["height"] = settings.width;
            collider["size"] = {settings.width, settings.width, settings.width};
            break;
        case ProceduralMeshKind::Capsule:
        case ProceduralMeshKind::Cylinder:
            // The backend contract currently exposes capsule as the
            // closest stable collider for round vertical primitives.
            collider["shape"] = "Capsule";
            collider["radius"] = settings.width * 0.5f;
            collider["height"] = settings.height;
            collider["size"] = {settings.width, settings.height, settings.width};
            break;
        case ProceduralMeshKind::Plane:
        case ProceduralMeshKind::GridPlane:
            collider["shape"] = "Box";
            collider["size"] = {settings.width, 0.02f, settings.depth};
            collider["radius"] = 0.01f;
            collider["height"] = 0.02f;
            break;
        case ProceduralMeshKind::Box:
        default:
            collider["shape"] = "Box";
            collider["size"] = {settings.width, settings.height, settings.depth};
            collider["radius"] = (std::min)({settings.width, settings.height, settings.depth}) * 0.5f;
            collider["height"] = settings.height;
            break;
        }
        object.components.push_back(SceneComponentData{"ColliderComponent", std::move(collider)});
    }

    NormalizeSceneComponentIds(object);

    scene.GetSceneDocument().objects.push_back(std::move(object));
    scene.RebuildRuntimeWorld();
    return FindRuntimeObject(scene, id);
}

GameObject* CreateWaterSurfaceObject(DocumentSceneBase& scene, const CreateObjectRequest& request) {
    CreateObjectRequest objectRequest = request;
    if (objectRequest.name.empty()) {
        objectRequest.name = "Water Surface";
    }

    SceneObjectData object = BuildBaseObject(scene, nullptr, objectRequest);
    const SceneObjectId id = object.id;

    object.components.push_back(SceneComponentData{"ModelComponent",
                                                   {{"assetId", ""},
                                                    {"visible", true},
                                                    {"castShadow", false},
                                                    {"receiveShadow", false},
                                                    {"renderStatic", false}}});
    object.components.push_back(SceneComponentData{"ProceduralMeshComponent",
                                                   {{"kind", "GridPlane"},
                                                    {"width", 200.0f},
                                                    {"height", 1.0f},
                                                    {"depth", 200.0f},
                                                    {"segmentsX", 300u},
                                                    {"segmentsY", 300u},
                                                    {"segmentsZ", 1u},
                                                    {"sphereSlices", 32u},
                                                    {"sphereStacks", 16u},
                                                    {"doubleSided", true},
                                                    {"generateTangents", true}}});

    nlohmann::json waterProperties = nlohmann::json::object();
    WaterSurfaceComponent waterDefaults{};
    waterDefaults.ApplyPreset(WATER::WaterSurfacePreset::AnimeSummer);
    waterDefaults.Serialize(waterProperties);
    object.components.push_back(SceneComponentData{"WaterSurfaceComponent", std::move(waterProperties)});

    NormalizeSceneComponentIds(object);
    scene.GetSceneDocument().objects.push_back(std::move(object));
    scene.RebuildRuntimeWorld();
    return FindRuntimeObject(scene, id);
}

GameObject* CreateSandSurfaceObject(DocumentSceneBase& scene, const CreateObjectRequest& request) {
    CreateObjectRequest objectRequest = request;
    if (objectRequest.name.empty()) {
        objectRequest.name = "Sand Surface";
    }

    SceneObjectData object = BuildBaseObject(scene, nullptr, objectRequest);
    const SceneObjectId id = object.id;

    object.components.push_back(SceneComponentData{
        "ModelComponent",
        {{"assetId", ""}, {"visible", true}, {"castShadow", false}, {"receiveShadow", true}, {"renderStatic", false}}});
    object.components.push_back(SceneComponentData{"ProceduralMeshComponent",
                                                   {{"kind", "GridPlane"},
                                                    {"width", 8.0f},
                                                    {"height", 1.0f},
                                                    {"depth", 8.0f},
                                                    {"segmentsX", 128u},
                                                    {"segmentsY", 128u},
                                                    {"segmentsZ", 1u},
                                                    {"sphereSlices", 32u},
                                                    {"sphereStacks", 16u},
                                                    {"doubleSided", true},
                                                    {"generateTangents", true}}});

    TerrainSurfaceComponent sandDefaults{};
    sandDefaults.ApplySolidPreset("sand");
    nlohmann::json sandProperties = nlohmann::json::object();
    sandDefaults.Serialize(sandProperties);
    object.components.push_back(SceneComponentData{"TerrainSurfaceComponent", std::move(sandProperties)});

    NormalizeSceneComponentIds(object);
    scene.GetSceneDocument().objects.push_back(std::move(object));
    scene.RebuildRuntimeWorld();
    return FindRuntimeObject(scene, id);
}

GameObject* CreateWorldSpriteObject(DocumentSceneBase& scene, const AssetGuid& textureGuid,
                                    const CreateObjectRequest& request) {
    SceneObjectData object = BuildBaseObject(scene, textureGuid.IsValid() ? &textureGuid : nullptr, request);
    if (request.name.empty() && !textureGuid.IsValid()) {
        object.name = "World Sprite";
    }
    const SceneObjectId id = object.id;
    object.components.push_back(SceneComponentData{"WorldSpriteComponent",
                                                   {{"version", 1u},
                                                    {"textureAssetGuid", textureGuid.value},
                                                    {"visible", true},
                                                    {"size", {1.0f, 1.0f}},
                                                    {"preserveAspect", true},
                                                    {"facing", "CameraFacing"},
                                                    {"tint", {1.0f, 1.0f, 1.0f}},
                                                    {"opacity", 1.0f},
                                                    {"pivot", {0.5f, 0.5f}},
                                                    {"alphaMode", "Auto"},
                                                    {"alphaCutoff", 0.5f},
                                                    {"transparentSortPriority", 0}}});
    NormalizeSceneComponentIds(object);
    scene.GetSceneDocument().objects.push_back(std::move(object));
    scene.RebuildRuntimeWorld();
    return FindRuntimeObject(scene, id);
}

GameObject* CreateWorldTextObject(DocumentSceneBase& scene, const CreateWorldTextRequest& request) {
    SceneObjectData object = BuildBaseObject(scene, nullptr, request.object);
    if (request.object.name.empty()) {
        object.name = "World Text";
    }
    const SceneObjectId id = object.id;

    WorldTextComponent component{};
    component.SetText(request.text);
    component.SetFontAssetPath(request.fontAssetPath);
    component.SetVisible(request.visible);
    component.SetLayoutSize(request.layoutSize);
    component.SetFontHeight(request.fontHeight);
    component.SetHorizontalAlign(request.horizontalAlign);
    component.SetVerticalAlign(request.verticalAlign);
    component.SetWrapMode(request.wrap);
    component.SetLineHeightMultiplier(request.lineHeightMultiplier);
    component.SetLetterSpacing(request.letterSpacing);
    component.SetFacingMode(request.facing);
    component.SetColor(request.color);
    component.SetOutlineColor(request.outlineColor);
    component.SetOutlineWidth(request.outlineWidth);
    component.SetOpacity(request.opacity);
    component.SetRevealAmount(request.revealAmount);
    component.SetPivot(request.pivot);
    component.SetTransparentSortPriority(request.transparentSortPriority);

    nlohmann::json properties = nlohmann::json::object();
    component.Serialize(properties);
    object.components.push_back(SceneComponentData{"WorldTextComponent", std::move(properties)});
    NormalizeSceneComponentIds(object);
    scene.GetSceneDocument().objects.push_back(std::move(object));

    // Document を正として runtime world を一度だけ作り直す。
    scene.RebuildRuntimeWorld();
    return FindRuntimeObject(scene, id);
}

GameObject* CreateDialogueTriggerObject(DocumentSceneBase& scene, const CreateObjectRequest& request) {
    CreateObjectRequest objectRequest = request;
    if (objectRequest.name.empty()) {
        objectRequest.name = "Dialogue Trigger";
    }

    SceneObjectData object = BuildBaseObject(scene, nullptr, objectRequest);
    const SceneObjectId id = object.id;

    object.components.push_back(SceneComponentData{"PhysicsBodyComponent",
                                                   {{"enabled", true},
                                                    {"motionType", "Static"},
                                                    {"mass", 1.0f},
                                                    {"gravityScale", 0.0f},
                                                    {"linearDamping", 0.0f},
                                                    {"angularDamping", 0.0f},
                                                    {"allowSleeping", true},
                                                    {"continuousCollision", false},
                                                    {"initialLinearVelocity", {0.0f, 0.0f, 0.0f}},
                                                    {"initialAngularVelocity", {0.0f, 0.0f, 0.0f}},
                                                    {"lockTranslation", {0.0f, 0.0f, 0.0f}},
                                                    {"lockRotation", {0.0f, 0.0f, 0.0f}}}});
    object.components.push_back(
        SceneComponentData{"ColliderComponent",
                           {{"enabled", true},
                            {"fitMode", "Manual"},
                            {"shape", "Box"},
                            {"collisionAssetId", ""},
                            {"center", {0.0f, 1.0f, 0.0f}},
                            {"rotation", {0.0f, 0.0f, 0.0f}},
                            {"size", {4.0f, 2.0f, 4.0f}},
                            {"radius", 1.0f},
                            {"height", 2.0f},
                            {"trigger", true},
                            {"material", {{"friction", 0.5f}, {"restitution", 0.0f}, {"density", 1.0f}}},
                            {"filter", {{"layer", 1u}, {"mask", 0xFFFFFFFFu}}}}});
    object.components.push_back(SceneComponentData{"DialogueTriggerComponent",
                                                   {{"enabled", true},
                                                    {"dialogueAssetGuid", ""},
                                                    {"locale", "ja-JP"},
                                                    {"activation", "OnEnter"},
                                                    {"interactAction", "Gameplay.Interact"},
                                                    {"pauseWorld", true},
                                                    {"oneShot", false}}});

    NormalizeSceneComponentIds(object);
    scene.GetSceneDocument().objects.push_back(std::move(object));
    scene.RebuildRuntimeWorld();
    return FindRuntimeObject(scene, id);
}

GameObject* CreateReflectionProbeObject(DocumentSceneBase& scene, const CreateObjectRequest& request) {
    CreateObjectRequest objectRequest = request;
    if (objectRequest.name.empty()) {
        objectRequest.name = "Reflection Probe";
    }
    LIGHTING_AUTHORING::ReflectionProbeAuthoring probe{};
    probe.name = objectRequest.name;
    probe.position = objectRequest.position;
    probe.settings.enabled = true;
    probe.settings.position = probe.position;
    probe.settings.influenceBoxCenter = probe.position;
    probe.settings.projectionBoxCenter = probe.position;
    const SceneObjectId id = LIGHTING_AUTHORING::AddReflectionProbe(scene.GetSceneDocument(), probe);
    scene.RebuildRuntimeWorld();
    return FindRuntimeObject(scene, id);
}

GameObject* CreateIrradianceVolumeObject(DocumentSceneBase& scene, const CreateObjectRequest& request) {
    CreateObjectRequest objectRequest = request;
    if (objectRequest.name.empty()) {
        objectRequest.name = "Irradiance Volume";
    }
    LIGHTING_AUTHORING::IrradianceVolumeAuthoring volume{};
    volume.name = objectRequest.name;
    volume.center = objectRequest.position;
    volume.settings.origin = {volume.center.x - volume.settings.size.x * 0.5f,
                              volume.center.y - volume.settings.size.y * 0.5f,
                              volume.center.z - volume.settings.size.z * 0.5f};
    const SceneObjectId id = LIGHTING_AUTHORING::AddIrradianceVolume(scene.GetSceneDocument(), volume);
    scene.RebuildRuntimeWorld();
    return FindRuntimeObject(scene, id);
}

GameObject* CreateLightObject(DocumentSceneBase& scene, const CreateObjectRequest& request,
                              RENDER3D::LIGHTING::SceneLightType lightType) {
    LIGHTING_AUTHORING::LightAuthoring light{};
    light.name = request.name;
    light.position = request.position;
    light.rotationEulerDeg = request.rotation;
    light.type = lightType;
    if (light.name.empty()) {
        switch (lightType) {
        case RENDER3D::LIGHTING::SceneLightType::Directional:
            light.name = "Directional Light";
            light.priority = 100;
            break;
        case RENDER3D::LIGHTING::SceneLightType::Spot:
            light.name = "Spot Light";
            light.range = 8.0f;
            break;
        case RENDER3D::LIGHTING::SceneLightType::Point:
        default: light.name = "Point Light"; break;
        }
    }
    const SceneObjectId id = LIGHTING_AUTHORING::AddLight(scene.GetSceneDocument(), light);
    scene.RebuildRuntimeWorld();
    return FindRuntimeObject(scene, id);
}

} // namespace HIKARI::EDITOR
