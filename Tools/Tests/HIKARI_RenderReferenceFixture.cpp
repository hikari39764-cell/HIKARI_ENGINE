#include <cmath>
#include <array>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "Assets/Environment/HIKARI_EnvironmentAsset.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Core/Content/HIKARI_EngineContentContext.h"
#include "RenderContracts/Material/HIKARI_MaterialAssetData.h"
#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Assets/Material/Compiler/HIKARI_MaterialCompiler.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/HIKARI_ProjectDescriptor.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettingsJson.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"
#if defined(HIKARI_REFERENCE_PROJECT)
#include "RenderReference/HIKARI_RenderValidationProject.h"
#include "Assets/Material/Serialization/HIKARI_MaterialSourceJson.h"
#endif

namespace {
using namespace HIKARI;

void Require(bool success, const std::string& message) {
    if (!success)
        throw std::runtime_error(message);
}

// 生成する検証プロジェクトの種類。舞台型（筆触・表面材質）は同じ床・カメラ・環境を共有する。
enum class Workload { Reference, Painterly, Surfaces };

AssetGuid Guid(unsigned value) {
    char text[37]{};
    std::snprintf(text, sizeof(text), "7255ce10541041008100%012u", value);
    return {text};
}

void WriteMeta(AssetDatabase& database, const std::filesystem::path& source, const AssetGuid& guid) {
    auto* importer = database.GetImporterRegistry().FindForSource(source);
    Require(importer != nullptr, "Missing reference asset importer");
    AssetRecord record{};
    record.sourcePath = source;
    record.metaPath = database.GetMetaPathForSource(source);
    record.meta = importer->CreateDefaultMeta(source, guid);
    Require(database.WriteMeta(record), "Could not write reference asset metadata");
}

SceneObjectData Mesh(unsigned id, MATH::Vec3 position, MATH::Vec3 scale, ProceduralMeshKind kind,
                     const AssetGuid& material) {
    SceneObjectData object{};
    object.id = {id};
    object.name = "Reference_" + std::to_string(id);
    object.transform.position = position;
    object.transform.scale = scale;
    ProceduralMeshComponent component{};
    ProceduralMeshSettings settings{};
    settings.kind = kind;
    settings.segmentsX = settings.segmentsY = settings.segmentsZ = 1;
    settings.sphereSlices = 32;
    settings.sphereStacks = 16;
    component.SetSettings(settings);
    component.SetMaterialSelection(material.value);
    SceneComponentData data{};
    data.id = {1};
    data.type = component.GetTypeName();
    component.Serialize(data.properties);
    object.components.push_back(std::move(data));
    ModelComponent model{};
    SceneComponentData modelData{};
    modelData.id = {2};
    modelData.type = model.GetTypeName();
    model.Serialize(modelData.properties);
    object.components.push_back(std::move(modelData));
    return object;
}

void WriteStageFloor(const std::filesystem::path& root, AssetDatabase& database) {
    PbrMaterialAssetData floor;
    floor.materialName = "MatteStage";
    floor.baseColorFactor = {0.16f, 0.18f, 0.2f, 1};
    floor.roughnessFactor = 0.85f;
    const std::filesystem::path path = "Assets/Materials/MatteStage.material.json";
    std::string error;
    Require(MATERIAL_SERIALIZATION::SaveMaterialSourceJson(root / path, MakeStandardPbrMaterialAssetData(floor), error),
            error);
    WriteMeta(database, path, Guid(164));
}

// 行 0（手前）：肌色の下地に次表面散乱 1、散乱半径 0.02 / 0.05 / 0.1 / 0.2 m（近いほど画素半径が大きく、明暗の境のにじみを
// 見やすい）。行 1：透過 1 の Blend 材質で屈折率 1.0 / 1.33 / 1.5 / 2.0。行 2：不透明の対照。行 3：赤い粗い下地に清漆 0→1。
void WriteSurfaceMaterials(const std::filesystem::path& root, AssetDatabase& database) {
    constexpr std::array labels{"Skin", "Glass", "Reference", "Coat"};
    constexpr std::array iors{1.0f, 1.33f, 1.5f, 2.0f};
    constexpr std::array scatterRadii{0.02f, 0.05f, 0.1f, 0.2f};
    const MATH::Vec4 references[] = {
        {0.5f, 0.5f, 0.5f, 1}, {0.8f, 0.12f, 0.06f, 1}, {0.08f, 0.45f, 0.7f, 1}, {0.95f, 0.7f, 0.25f, 1}};
    for (unsigned row = 0; row < labels.size(); ++row) {
        for (unsigned column = 0; column < 4u; ++column) {
            PbrMaterialAssetData pbr{};
            pbr.materialName = std::string(labels[row]) + "_" + std::to_string(column);
            if (row == 0) {
                pbr.baseColorFactor = {0.85f, 0.55f, 0.45f, 1};
                pbr.roughnessFactor = 0.5f;
                pbr.subsurfaceFactor = 1.0f;
                pbr.subsurfaceRadius = scatterRadii[column];
            } else if (row == 1) {
                pbr.baseColorFactor = {0.95f, 0.97f, 1.0f, 1};
                pbr.roughnessFactor = 0.05f;
                pbr.transmissionFactor = 1.0f;
                pbr.ior = iors[column];
            } else if (row == 2) {
                pbr.baseColorFactor = references[column];
                pbr.roughnessFactor = 0.35f;
            } else {
                pbr.baseColorFactor = {0.55f, 0.06f, 0.05f, 1};
                pbr.roughnessFactor = 0.55f;
                pbr.clearCoatFactor = static_cast<float>(column) / 3.0f;
                pbr.clearCoatRoughnessFactor = 0.1f;
            }
            MaterialAssetData material = MakeStandardPbrMaterialAssetData(pbr);
            if (row == 1)
                material.surface.alphaMode = MaterialAlphaMode::Blend;
            const auto path = std::filesystem::path("Assets/Materials") / (pbr.materialName + ".material.json");
            std::string error;
            Require(MATERIAL_SERIALIZATION::SaveMaterialSourceJson(root / path, material, error), error);
            WriteMeta(database, path, Guid(100 + row * 4 + column));
        }
    }
    WriteStageFloor(root, database);
}

void WritePainterlyMaterials(const std::filesystem::path& root, AssetDatabase& database) {
    constexpr std::array layers{0u, 2u, 4u, 8u};
    for (size_t column = 0; column < layers.size(); ++column) {
        PbrMaterialAssetData pbr{};
        pbr.baseColorFactor = {0.12f, 0.42f, 0.62f, 1};
        pbr.roughnessFactor = 0.75f;
        const auto name = "Painterly_" + std::to_string(layers[column]) + "_Layers";
        auto material =
            ASSETS::MATERIAL::CreateMaterialFromTemplateWithPbr(MaterialTemplateId::PainterlyShell, pbr, name);
        const auto set = [&](std::string_view id, float value) {
            const auto parameter = std::find_if(material.graph.parameters.begin(), material.graph.parameters.end(),
                                                [&](const auto& entry) { return entry.id == id; });
            Require(parameter != material.graph.parameters.end(),
                    "Missing painterly template parameter: " + std::string(id));
            parameter->defaultValue.numeric.x = value;
        };
        set("parameter.shellMaxLayers", static_cast<float>(layers[column]));
        set("parameter.shellThickness", 0.045f);
        set("parameter.shellLodStartPixels", 100.0f);
        set("parameter.shellLodFullPixels", 260.0f);
        set("parameter.strokeTiling", 1.0f);
        set("parameter.strokeCoverage", 0.82f);
        set("parameter.strokeEdgePower", 2.5f);
        const auto compiled = CompileMaterialAsset(material);
        if (!compiled.success) {
            for (const auto& diagnostic : compiled.diagnostics)
                std::cerr << diagnostic.message << '\n';
        }
        Require(compiled.success, "Painterly fixture material did not compile");
        const auto path = std::filesystem::path("Assets/Materials") / (name + ".material.json");
        std::string error;
        Require(MATERIAL_SERIALIZATION::SaveMaterialSourceJson(root / path, material, error), error);
        WriteMeta(database, path, Guid(100 + static_cast<unsigned>(column)));
    }
    WriteStageFloor(root, database);
}

void Generate(const std::filesystem::path& root, unsigned count, Workload workload = Workload::Reference) {
    const bool painterly = workload == Workload::Painterly;
    const bool surfaces = workload == Workload::Surfaces;
    const bool staged = painterly || surfaces;
    // 既存プロジェクトは上書きしない。生成物の所有範囲は空の出力先のみ。
    Require(!std::filesystem::exists(root) || std::filesystem::is_empty(root),
            "Output must be a new or empty directory");
    std::filesystem::create_directories(root / "Assets/Materials");
    std::filesystem::create_directories(root / "Assets/Scenes");
    ProjectDescriptor descriptor{};
    descriptor.name = painterly  ? "PainterlyShell"
                      : surfaces ? "SurfaceMaterials"
                                 : "RenderReference_" + std::to_string(count);
    std::string error{};
    Require(SaveProjectDescriptor(root / "Project.hikari", descriptor, &error), error);
    std::ofstream(root / "CMakeLists.txt")
        << "# Built-in render fixture; launch Editor with --allow-missing-project-module.\n";

    AssetDatabase database{};
    Require(database.Initialize(root), "Could not initialize reference asset database");
    // 筆触材質はエンジン内蔵テクスチャ（engine.painterly.*）に依存する。取り込みはその目録で検証する。
    Require(CONTENT::GetEngineContentContext().Open(HIKARI_CONTRACT_ENGINE_CONTENT_ROOT),
            "Could not open the engine content root");
    if (painterly)
        WritePainterlyMaterials(root, database);
    if (surfaces)
        WriteSurfaceMaterials(root, database);
    for (unsigned i = 0; !staged && i < 65; ++i) {
        PbrMaterialAssetData pbr{};
        pbr.materialName = "Pbr_" + std::to_string(i);
        const unsigned row = i / 8;
        pbr.roughnessFactor = i == 64 ? 0.6f : 0.04f + 0.96f * (i % 8) / 7.0f;
        pbr.metallicFactor = row >= 4 && i != 64 ? 1.0f : 0.0f;
        const MATH::Vec4 colors[] = {
            {0.5f, 0.5f, 0.5f, 1}, {0.8f, 0.12f, 0.06f, 1}, {0.08f, 0.45f, 0.7f, 1}, {0.95f, 0.7f, 0.25f, 1}};
        pbr.baseColorFactor = i == 64 ? MATH::Vec4{0.18f, 0.18f, 0.18f, 1} : colors[row % 4];
        const auto path = std::filesystem::path("Assets/Materials") / (pbr.materialName + ".material.json");
        Require(
            MATERIAL_SERIALIZATION::SaveMaterialSourceJson(root / path, MakeStandardPbrMaterialAssetData(pbr), error),
            error);
        WriteMeta(database, path, Guid(100 + i));
    }

    SceneDocument document{};
    document.sceneName = descriptor.name;
    // 時刻に依らない固定の太陽（光線 {-0.5, -1, -0.65}）と固定の露出で、毎回同じ画面にする。
    ENVIRONMENT::EnvironmentProfileAsset environmentAsset{};
    environmentAsset.name = "Reference";
    EnvironmentProfile& environment = environmentAsset.profile;
    environment.sun.mode = SunMode::Fixed;
    environment.sun.fixedAzimuthDegrees = 142.4f;
    environment.sun.fixedElevationDegrees = 50.7f;
    environment.exposure.mode = RENDER3D::COLOR::ExposureMode::Manual;
    environment.exposure.manualEv100 = 14.0f;
    environment.shadows.strength = 1.0f;
    const unsigned columns = static_cast<unsigned>(std::ceil(std::sqrt(count)));
    const float extent = staged ? 80.0f : columns * 1.5f;
    environment.shadows.distance = extent * 3.0f;
    document.objects.reserve(count + 3);
    if (painterly) {
        constexpr std::array distances{0.0f, 16.0f, 64.0f};
        constexpr std::array labels{"Near", "Medium", "Far"};
        constexpr std::array layers{0u, 2u, 4u, 8u};
        for (unsigned row = 0; row < distances.size(); ++row) {
            for (unsigned column = 0; column < layers.size(); ++column) {
                auto object =
                    Mesh(10 + row * 4 + column, {(static_cast<float>(column) - 1.5f) * 3.0f, 1.3f, distances[row]},
                         {2.4f, 2.4f, 2.4f}, ProceduralMeshKind::Sphere, Guid(100 + column));
                object.name = std::string(labels[row]) + "_" + std::to_string(layers[column]) + "_Layers";
                object.editorFolder = labels[row];
                document.objects.push_back(std::move(object));
            }
        }
        document.editorFolders = {"Near", "Medium", "Far"};
    }
    if (surfaces) {
        constexpr std::array distances{0.0f, 8.0f, 16.0f, 24.0f};
        constexpr std::array labels{"Skin", "Glass", "Reference", "Coat"};
        for (unsigned row = 0; row < distances.size(); ++row) {
            for (unsigned column = 0; column < 4u; ++column) {
                auto object =
                    Mesh(10 + row * 4 + column, {(static_cast<float>(column) - 1.5f) * 3.0f, 1.3f, distances[row]},
                         {2.4f, 2.4f, 2.4f}, ProceduralMeshKind::Sphere, Guid(100 + row * 4 + column));
                object.name = std::string(labels[row]) + "_" + std::to_string(column);
                object.editorFolder = labels[row];
                document.objects.push_back(std::move(object));
            }
        }
        document.editorFolders = {"Skin", "Glass", "Reference", "Coat"};
    }
    if (staged) {
        environment.clouds.enabled = false;
        environment.fog.enabled = false;
        environment.bloom.enabled = false;
        environment.screenSpaceReflections.enabled = false;
    }
    for (unsigned i = 0; !staged && i < count; ++i) {
        document.objects.push_back(Mesh(
            i + 10, {(i % columns + 0.5f) * 1.5f - extent * 0.5f, 0.5f, (i / columns + 0.5f) * 1.5f - extent * 0.5f},
            {1, 1, 1}, count == 64 ? ProceduralMeshKind::Sphere : ProceduralMeshKind::Box, Guid(100 + i % 64)));
    }
    document.objects.push_back(Mesh(2, {0, -0.15f, staged ? 32.0f : 0.0f}, {extent + 4, 0.3f, extent + 4},
                                    ProceduralMeshKind::Box, Guid(164)));
    SceneObjectData camera{};
    camera.id = {1};
    camera.name = "ReferenceCamera";
    camera.transform.position = staged ? MATH::Vec3{0, 6.0f, -12.0f} : MATH::Vec3{0, extent * 0.95f, -extent * 1.45f};
    camera.transform.rotationEulerDeg = {staged ? 17.0f : 32.0f, 0, 0};
    camera.components.push_back(
        {"CameraComponent",
         {{"enabled", true}, {"verticalFovDegrees", 50.0f}, {"nearClip", 0.1f}, {"farClip", extent * 8.0f}},
         {1}});
    document.camera.defaultCameraObjectId = camera.id;
    document.objects.push_back(std::move(camera));
    const std::filesystem::path environmentPath = "Assets/Environment/Reference.henvironment";
    Require(ENVIRONMENT::SaveEnvironmentProfileAsset(root / environmentPath, environmentAsset, error), error);
    WriteMeta(database, environmentPath, Guid(2));
    document.environment.profileId = Guid(2).value;
    const std::filesystem::path scenePath = "Assets/Scenes/Reference.scene.json";
    Require(SceneSerializer{}.SaveToFile(TEXT::PathToUtf8(root / scenePath), document),
            "Could not save reference scene");
    WriteMeta(database, scenePath, Guid(1));
    // 実行時は取り込み済みの資産だけを使う。生成した資産をここで取り込んでおく。
    Require(database.ScanAssets(false), "Reference asset scan failed");
    Require(database.ImportAllOutdated().failed == 0, "Reference asset import failed");
    ProjectSettingsService projectSettings{};
    Require(projectSettings.Load(root), "Could not load reference settings");
    Require(projectSettings.SetStartupSceneGuid(Guid(1)), "Invalid reference startup scene GUID");
    projectSettings.SetEnabledRuntimeFeatures({"Rendering", "Camera", "Lighting"});
    Require(projectSettings.Save(), "Could not save reference settings");
    RENDER3D::RenderQualitySettings quality{};
    quality.sceneResolution = RENDER3D::RenderResolutionPreset::P1080;
    quality.windowSize = RENDER3D::RenderResolutionPreset::P1080;
    quality.antiAliasingMode = RENDER3D::RenderAntiAliasingMode::TAA;
    quality.frameGenerationMode = RENDER3D::RenderFrameGenerationMode::Off;
    quality.vSync = false;
    quality.directionalShadow.farPlane = extent * 4.0f;
    nlohmann::json runtimeConfig{{"runtime",
                                  {{"enableDebugLayer", false},
                                   {"enableDebugCamera", false},
                                   {"windowWidth", 1920},
                                   {"windowHeight", 1080},
                                   {"renderQuality", RENDER3D::SerializeRenderQualitySettings(quality)}}}};
    Require(SERIALIZATION::JSON::WriteJsonFile(root / "runtime_config.json", runtimeConfig, &error), error);
    if (staged) {
        // 通常の Editor 起動は UI を保持し、明示的な画像検証だけ UI を外す。
        runtimeConfig["runtime"]["enableEditorUI"] = false;
        runtimeConfig["runtime"]["enableImGui"] = false;
        Require(SERIALIZATION::JSON::WriteJsonFile(root / "Capture.runtime.json", runtimeConfig, &error), error);
    }
    Require(SERIALIZATION::JSON::WriteJsonFile(
                root / "ReferenceFixture.json",
                {{"version", 1},
                 {"meshCount", count + 1},
                 {"objectCount", document.objects.size()},
                 {"source", "HIKARI_RenderReferenceFixture"},
                 {"workload", painterly     ? "painterly-shell"
                              : surfaces    ? "surface-materials"
                              : count == 64 ? "pbr-grid"
                                            : "shadowed-gameobjects"},
                 {"skinnedMeshes", false},
                 {"shellLayersByColumn", painterly ? nlohmann::json{0, 2, 4, 8} : nlohmann::json::array()},
                 {"shellRows", painterly ? nlohmann::json{"Near", "Medium", "Far"} : nlohmann::json::array()},
                 {"cameraObjectId", 1},
                 {"sceneGuid", Guid(1).value}},
                &error),
            error);
    std::cout << "Generated " << count << " reference objects: " << TEXT::PathToUtf8(root) << '\n';
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 3 && std::wstring_view(argv[1]) == L"--painterly") {
            Generate(std::filesystem::absolute(argv[2]), 12, Workload::Painterly);
            return 0;
        }
        if (argc == 3 && std::wstring_view(argv[1]) == L"--surfaces") {
            Generate(std::filesystem::absolute(argv[2]), 12, Workload::Surfaces);
            return 0;
        }
#if defined(HIKARI_REFERENCE_PROJECT)
        if (argc == 4 && std::wstring_view(argv[1]) == L"--project") {
            HIKARI::REFERENCE::CreateValidationProject(argv[2], argv[3]);
            return 0;
        }
        if (argc == 3 && std::wstring_view(argv[1]) == L"--verify-project") {
            HIKARI::REFERENCE::VerifyValidationProject(argv[2]);
            return 0;
        }
#endif
        if (argc != 3)
            throw std::runtime_error(
                "Usage: HIKARI_RenderReferenceFixture <empty-output-directory> <64|100|1000|10000>, "
                "--painterly <empty-output-directory> or --surfaces <empty-output-directory>");
        const std::wstring countText(argv[2]);
        if (countText != L"64" && countText != L"100" && countText != L"1000" && countText != L"10000") {
            throw std::runtime_error("Unsupported bounded reference object count");
        }
        Generate(std::filesystem::absolute(argv[1]), std::stoul(countText));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
