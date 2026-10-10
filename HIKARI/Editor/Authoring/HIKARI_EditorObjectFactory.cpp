#include "HIKARI_EditorObjectFactory.h"

#include <memory>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Sprite/HIKARI_SpriteAssetStore.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SceneComponentId.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/Rendering/Decal/HIKARI_DecalComponent.h"
#include "Scene/Components/Rendering/WorldSprite/HIKARI_WorldSpriteComponent.h"
#include "Scene/Components/Rendering/WorldText/HIKARI_WorldTextComponent.h"
#include "Scene/Lighting/HIKARI_LightingAuthoringModel.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"
#include "Scene/Water/HIKARI_WaterSurfaceComponent.h"

namespace HIKARI::EDITOR {

namespace {
GameObject* FindRuntimeObject(DocumentSceneBase& scene, SceneObjectId id) { return scene.GetWorld().FindObject(id); }

// document に足した一つのオブジェクトだけを runtime world へ反映する。
GameObject* MaterializeCreatedObject(DocumentSceneBase& scene, SceneObjectId id) {
    (void)scene.ApplyRuntimeObjectChanges({.added = {id}});
    return FindRuntimeObject(scene, id);
}

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

    return MaterializeCreatedObject(scene, id);
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

    return MaterializeCreatedObject(scene, id);
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
        case ProceduralMeshKind::Ramp:
        case ProceduralMeshKind::Stairs: {
            MATH::Vec3 center{};
            MATH::Vec3 rotation{};
            MATH::Vec3 size{};
            FitProceduralSlopeBox(settings, center, rotation, size);
            collider["shape"] = "Box";
            collider["center"] = {center.x, center.y, center.z};
            collider["rotation"] = {rotation.x, rotation.y, rotation.z};
            collider["size"] = {size.x, size.y, size.z};
            collider["radius"] = size.y * 0.5f;
            collider["height"] = size.y;
            break;
        }
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
    return MaterializeCreatedObject(scene, id);
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
    return MaterializeCreatedObject(scene, id);
}

GameObject* CreateTerrainSurfaceObject(DocumentSceneBase& scene, const CreateObjectRequest& request) {
    CreateObjectRequest objectRequest = request;
    if (objectRequest.name.empty()) {
        objectRequest.name = "Terrain";
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

    TerrainSurfaceComponent terrainDefaults{};
    terrainDefaults.ApplySolidPreset("sand");
    nlohmann::json terrainProperties = nlohmann::json::object();
    terrainDefaults.Serialize(terrainProperties);
    object.components.push_back(SceneComponentData{"TerrainSurfaceComponent", std::move(terrainProperties)});

    NormalizeSceneComponentIds(object);
    scene.GetSceneDocument().objects.push_back(std::move(object));
    return MaterializeCreatedObject(scene, id);
}

GameObject* CreateWorldSpriteObject(DocumentSceneBase& scene, const AssetGuid& assetGuid,
                                    const CreateObjectRequest& request) {
    SceneObjectData object = BuildBaseObject(scene, assetGuid.IsValid() ? &assetGuid : nullptr, request);
    if (request.name.empty() && !assetGuid.IsValid()) {
        object.name = "World Sprite";
    }

    WorldSpriteComponent sprite{};
    bool enableSpriteLighting = false;
    if (assetGuid.IsValid()) {
        const AssetRecord* record = scene.GetAssetDatabase().FindByGuid(assetGuid);
        if (record != nullptr && record->type == AssetType::Sprite) {
            if (!record->lastImportSucceeded) {
                return nullptr;
            }
            SPRITE::SpriteAssetStore store{};
            store.Configure(&scene.GetAssetDatabase());
            std::string error{};
            const std::shared_ptr<const SPRITE::SpriteAssetData> asset = store.Load(assetGuid, error);
            if (asset == nullptr || !sprite.BindSpriteAsset(assetGuid, asset)) {
                return nullptr;
            }
            for (const SPRITE::SpritePage& page : asset->pages) {
                enableSpriteLighting = enableSpriteLighting || !page.normalTextureAssetId.empty();
            }
        } else {
            sprite.SetTextureAssetGuid(assetGuid);
        }
    }

    nlohmann::json properties = nlohmann::json::object();
    sprite.Serialize(properties);
    if (enableSpriteLighting) {
        properties["lit"] = true;
    }
    const SceneObjectId id = object.id;
    object.components.push_back(SceneComponentData{"WorldSpriteComponent", std::move(properties)});
    NormalizeSceneComponentIds(object);
    scene.GetSceneDocument().objects.push_back(std::move(object));
    return MaterializeCreatedObject(scene, id);
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

    return MaterializeCreatedObject(scene, id);
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
    return MaterializeCreatedObject(scene, id);
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
    return MaterializeCreatedObject(scene, id);
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
    return MaterializeCreatedObject(scene, id);
}

GameObject* CreateLightObject(DocumentSceneBase& scene, const CreateObjectRequest& request,
                              RENDER3D::LIGHTING::SceneLightType lightType, LightIntensityUnit unit) {
    LIGHTING_AUTHORING::LightAuthoring light{};
    light.name = request.name;
    light.position = request.position;
    light.rotationEulerDeg = request.rotation;
    light.settings.type = lightType;
    light.settings.intensityUnit = unit;
    if (lightType == RENDER3D::LIGHTING::SceneLightType::Spot) {
        light.settings.range = 8.0f;
    }
    if (light.name.empty()) {
        light.name = std::string(TEXT::EnumToName(RENDER3D::LIGHTING::kSceneLightTypeNames, lightType)) + " Light";
    }
    const SceneObjectId id = LIGHTING_AUTHORING::AddLight(scene.GetSceneDocument(), light);
    return MaterializeCreatedObject(scene, id);
}

GameObject* CreateDecalObject(DocumentSceneBase& scene, const CreateObjectRequest& request) {
    SceneObjectData object = BuildBaseObject(scene, nullptr, request);
    if (request.name.empty()) {
        object.name = "Decal";
    }
    const SceneObjectId id = object.id;
    nlohmann::json properties = nlohmann::json::object();
    DecalComponent{}.Serialize(properties);
    object.components.push_back(SceneComponentData{"DecalComponent", std::move(properties)});
    NormalizeSceneComponentIds(object);
    scene.GetSceneDocument().objects.push_back(std::move(object));
    return MaterializeCreatedObject(scene, id);
}

GameObject* CreateCameraObject(DocumentSceneBase& scene, const CreateObjectRequest& request, float verticalFovDegrees) {
    SceneObjectData object = BuildBaseObject(scene, nullptr, request);
    if (request.name.empty()) {
        object.name = "Camera";
    }
    const SceneObjectId id = object.id;
    nlohmann::json properties = nlohmann::json::object();
    CameraComponent{}.Serialize(properties);
    properties["verticalFovDegrees"] = verticalFovDegrees;
    object.components.push_back(SceneComponentData{"CameraComponent", std::move(properties)});
    NormalizeSceneComponentIds(object);
    scene.GetSceneDocument().objects.push_back(std::move(object));
    return MaterializeCreatedObject(scene, id);
}

} // namespace HIKARI::EDITOR
