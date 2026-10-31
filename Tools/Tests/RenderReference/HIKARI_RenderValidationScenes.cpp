#include "HIKARI_RenderValidationProject.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include "Assets/Environment/HIKARI_EnvironmentAsset.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Render3D/Color/HIKARI_ExposureState.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/Components/Lighting/HIKARI_LightComponent.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"

namespace HIKARI::REFERENCE {
namespace {
constexpr float kDegrees = 180.0f / std::numbers::pi_v<float>;
// 屋内スタジオ相当の固定露出。空と太陽はこの露出で以前の表示の明るさになるよう較正する。
constexpr float kStudioEv100 = 8.0f;

SceneObjectData Object(SceneDocument& scene, std::string name, std::string folder) {
    SceneObjectData object;
    object.id = {scene.objects.size() + 1};
    object.name = std::move(name);
    object.editorFolder = std::move(folder);
    return object;
}

void Component(SceneObjectData& object, const IComponent& component) {
    SceneComponentData data;
    data.type = component.GetTypeName();
    data.id = {object.components.size() + 1};
    component.Serialize(data.properties);
    object.components.push_back(std::move(data));
}

// シーンごとの環境プロファイル。固定の太陽（光線 {-0.5, -1, -0.6}）と HDR の空、固定露出。
AssetGuid Environment(const std::filesystem::path& root, AssetDatabase& database, const std::string& name,
                      bool reflections, float shadowDistance) {
    ENVIRONMENT::EnvironmentProfileAsset asset;
    asset.name = name;
    EnvironmentProfile& env = asset.profile;
    const float exposure = RENDER3D::COLOR::ExposureFromEv100(kStudioEv100);
    env.exposure.mode = RENDER3D::COLOR::ExposureMode::Manual;
    env.exposure.manualEv100 = kStudioEv100;
    env.sky.source = SkySource::Hdri;
    env.sky.asset = ReferenceGuid("Assets/Skies/ReferenceStudio.dds").value;
    env.sky.textureIlluminance = std::numbers::pi_v<float> / exposure;
    env.sun.mode = SunMode::Fixed;
    env.sun.fixedAzimuthDegrees = 140.2f;
    env.sun.fixedElevationDegrees = 52.0f;
    env.sun.illuminance = 2.0f / exposure;
    env.sun.showDisk = false;
    env.moon.enabled = false;
    env.stars.enabled = false;
    env.shadows.strength = 1.0f;
    env.shadows.distance = shadowDistance;
    env.clouds.enabled = false;
    env.fog.enabled = false;
    env.bloom.enabled = false;
    env.ambientOcclusion.enabled = reflections;
    env.screenSpaceReflections.enabled = reflections;
    env.screenSpaceReflections.intensity = 1.0f;
    const std::filesystem::path path = "Assets/Environment/" + name + ".henvironment";
    std::string error;
    Require(ENVIRONMENT::SaveEnvironmentProfileAsset(root / path, asset, error), error);
    WriteReferenceMeta(database, path);
    return ReferenceGuid(path);
}

SceneDocument Scene(std::string name, const AssetGuid& environment) {
    SceneDocument result;
    result.sceneName = std::move(name);
    result.editorFolders = {"Models", "Stage", "Lighting", "Cameras"};
    result.environment.profileId = environment.value;
    return result;
}

void Camera(SceneDocument& scene, MATH::Vec3 eye, MATH::Vec3 target, float farClip) {
    auto object = Object(scene, "ValidationCamera", "Cameras");
    object.transform.position = eye;
    const auto direction = MATH::Normalize(target - eye);
    const float pitch = -std::asin(std::clamp(direction.y, -1.0f, 1.0f));
    const float yaw = std::atan2(direction.x, direction.z);
    object.transform.rotationEulerDeg = {pitch * kDegrees, yaw * kDegrees, 0};
    const auto matrix = MATH::Mat4::Rotate(MATH::Quat::FromEulerXYZ(pitch, yaw, 0));
    const auto forward = matrix.TransformPoint({0, 0, 1, 0});
    Require(MATH::Dot({forward.x, forward.y, forward.z}, direction) > 0.999f,
            "Camera convention does not match target");
    CameraComponent camera;
    camera.Deserialize({{"verticalFovDegrees", 45.0f}, {"nearClip", 0.05f}, {"farClip", farClip}});
    Component(object, camera);
    scene.camera.defaultCameraObjectId = object.id;
    scene.objects.push_back(std::move(object));
}

void Box(SceneDocument& scene, std::string name, MATH::Vec3 center, MATH::Vec3 scale, std::string_view material) {
    auto object = Object(scene, std::move(name), "Stage");
    object.transform.position = center;
    object.transform.scale = scale;
    ProceduralMeshComponent component;
    ProceduralMeshSettings settings;
    settings.kind = ProceduralMeshKind::Box;
    settings.segmentsX = settings.segmentsY = settings.segmentsZ = 1;
    component.SetSettings(settings);
    component.SetMaterialSelection(ReferenceGuid("Assets/Materials/" + std::string(material) + ".material.json").value);
    Component(object, component);
    scene.objects.push_back(std::move(object));
}

void Model(SceneDocument& scene, const ReferenceModel& model, MATH::Vec3 center, float size,
           MATH::Vec3 stretch = {1, 1, 1}, float yawDegrees = 0) {
    auto object = Object(scene, model.name + "_" + std::to_string(scene.objects.size()), "Models");
    const auto extent = model.bounds.max - model.bounds.min;
    const float largest = (std::max)({extent.x, extent.y, extent.z});
    Require(std::isfinite(largest) && largest > 1.0e-5f, "Invalid model bounds: " + model.name);
    const float fit = size / largest;
    object.transform.scale = stretch * fit;
    object.transform.rotationEulerDeg.y = yawDegrees;
    const auto sourceCenter = (model.bounds.min + model.bounds.max) * 0.5f;
    const auto matrix =
        MATH::Mat4::TRS({}, MATH::Quat::FromEulerXYZ(0, yawDegrees / kDegrees, 0), object.transform.scale);
    const auto offset = matrix.TransformPoint({sourceCenter.x, sourceCenter.y, sourceCenter.z, 0});
    object.transform.position = center - MATH::Vec3{offset.x, offset.y, offset.z};
    ModelComponent component;
    component.SetAssetId(model.guid.value);
    component.SetRenderStatic(true);
    component.SetIncludeInLightingBake(false);
    Component(object, component);
    scene.objects.push_back(std::move(object));
}

void Point(SceneDocument& scene, MATH::Vec3 position, MATH::Vec3 color) {
    auto object = Object(scene, "PointLight_" + std::to_string(scene.objects.size()), "Lighting");
    object.transform.position = position;
    LightSettings settings{};
    settings.type = RENDER3D::LIGHTING::SceneLightType::Point;
    settings.color = color;
    // 4 m 先で、固定露出の表示がおよそ 5 になる光束 [lm]。
    settings.intensity = 180000.0f;
    settings.range = 12.0f;
    LightComponent component;
    component.SetSettings(settings);
    Component(object, component);
    scene.objects.push_back(std::move(object));
}

void Save(const std::filesystem::path& root, AssetDatabase& database, const SceneDocument& scene,
          nlohmann::json& entries, unsigned modelCount, bool reflections = false) {
    const std::filesystem::path path = "Assets/Scenes/" + scene.sceneName + ".scene.json";
    Require(SceneSerializer{}.SaveToFile(TEXT::PathToUtf8(root / path), scene), "Scene save failed");
    WriteReferenceMeta(database, path);
    entries.push_back({{"name", scene.sceneName},
                       {"path", path.generic_string()},
                       {"guid", ReferenceGuid(path).value},
                       {"objectCount", scene.objects.size()},
                       {"modelInstanceCount", modelCount},
                       {"ssr", reflections},
                       {"cameraObjectId", scene.camera.defaultCameraObjectId->value}});
}
} // namespace

nlohmann::json CreateValidationScenes(const std::filesystem::path& root, AssetDatabase& database,
                                      const std::vector<ReferenceModel>& models) {
    std::filesystem::create_directories(root / "Assets/Materials");
    std::filesystem::create_directories(root / "Assets/Scenes");
    std::filesystem::create_directories(root / "Assets/Environment");
    const AssetGuid studioEnvironment = Environment(root, database, "Studio", false, 40.0f);
    const auto get = [&](std::string_view name) -> const ReferenceModel& {
        const auto found =
            std::find_if(models.begin(), models.end(), [&](const auto& model) { return model.name == name; });
        Require(found != models.end(), "Missing suite model: " + std::string(name));
        return *found;
    };
    for (const auto* name : {"MatteStage", "MirrorFloor", "RedBlock", "BlueBlock"}) {
        PbrMaterialAssetData data;
        data.materialName = name;
        data.baseColorFactor = {0.18f, 0.18f, 0.18f, 1};
        data.roughnessFactor = 0.65f;
        if (data.materialName == "MirrorFloor") {
            data.baseColorFactor = {0.7f, 0.7f, 0.7f, 1};
            data.metallicFactor = 1.0f;
            data.roughnessFactor = 0.12f;
        } else if (data.materialName == "RedBlock")
            data.baseColorFactor = {0.7f, 0.06f, 0.03f, 1};
        else if (data.materialName == "BlueBlock")
            data.baseColorFactor = {0.025f, 0.18f, 0.7f, 1};
        const std::filesystem::path path = "Assets/Materials/" + data.materialName + ".material.json";
        std::string error;
        Require(SaveMaterialAssetData(root / path, MakeStandardPbrMaterialAssetData(data), error), error);
        WriteReferenceMeta(database, path);
    }
    nlohmann::json entries = nlohmann::json::array();
    auto studio = Scene("01_MaterialStudio", studioEnvironment);
    Model(studio, get("FlightHelmet"), {-2.8f, 1.65f, 0}, 2.4f);
    Model(studio, get("WaterBottle"), {0, 1.3f, 0}, 1.8f);
    Model(studio, get("MetalRoughSpheres"), {3.0f, 1.9f, 0}, 4.0f);
    Box(studio, "Stage", {0, -0.15f, 0}, {16, 0.3f, 12}, "MatteStage");
    Camera(studio, {0, 5.0f, 14}, {0, 1.8f, 0}, 100);
    Save(root, database, studio, entries, 3);

    auto surface = Scene("02_SurfaceContracts", studioEnvironment);
    Model(surface, get("NormalTangentTest"), {-4, 2.0f, 0}, 3.5f);
    Model(surface, get("NormalTangentMirrorTest"), {0, 2.0f, 0}, 3.5f);
    Model(surface, get("AlphaBlendModeTest"), {4, 2.0f, 0}, 3.5f);
    Model(surface, get("NormalTangentMirrorTest"), {-2, 1.3f, -4}, 2.5f, {-1, 1, 1});
    Model(surface, get("NormalTangentTest"), {2, 1.3f, -4}, 2.5f, {1, 0.6f, 1.4f}, 25);
    Box(surface, "ShadowReceiver", {0, -0.15f, 0}, {18, 0.3f, 14}, "MatteStage");
    Camera(surface, {0, 5.5f, 17}, {0, 1.7f, -1}, 100);
    Save(root, database, surface, entries, 5);

    auto reflections = Scene("03_ReflectionsAndLights", Environment(root, database, "StudioReflections", true, 40.0f));
    Model(reflections, get("FlightHelmet"), {-2, 1.25f, 0}, 2.0f);
    Model(reflections, get("WaterBottle"), {2, 1.25f, 0}, 2.0f);
    Box(reflections, "ReflectiveFloor", {0, -0.15f, 0}, {16, 0.3f, 16}, "MirrorFloor");
    Box(reflections, "RedOccluder", {-4, 1.5f, -2}, {1, 3, 1}, "RedBlock");
    Box(reflections, "BlueOccluder", {4, 1.5f, -2}, {1, 3, 1}, "BlueBlock");
    Point(reflections, {-3, 4, 3}, {1, 0.8f, 0.6f});
    Point(reflections, {3, 4, 1}, {0.6f, 0.8f, 1});
    Camera(reflections, {0, 4, 11}, {0, 1, 0}, 100);
    Save(root, database, reflections, entries, 2, true);
    for (const unsigned count : {100u, 1000u, 5000u, 10000u}) {
        const std::string name = "Stress_" + std::to_string(count);
        const unsigned columns = static_cast<unsigned>(std::ceil(std::sqrt(count)));
        const float extent = columns * 1.5f;
        auto stress = Scene(name, Environment(root, database, name, false, extent * 4));
        for (unsigned i = 0; i < count; ++i) {
            Model(stress, get("WaterBottle"),
                  {(i % columns + 0.5f) * 1.5f - extent * 0.5f, 0.6f, (i / columns + 0.5f) * 1.5f - extent * 0.5f},
                  1.0f);
        }
        Box(stress, "ShadowReceiver", {0, -0.15f, 0}, {extent + 3, 0.3f, extent + 3}, "MatteStage");
        Camera(stress, {0, extent * 0.95f, extent * 1.6f}, {0, 0.6f, 0}, extent * 6);
        Save(root, database, stress, entries, count);
    }
    return entries;
}
} // namespace HIKARI::REFERENCE
