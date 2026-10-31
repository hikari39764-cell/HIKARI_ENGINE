#include "HIKARI_RenderValidationProject.h"

#include <Windows.h>
#include <DirectXTex.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Formats/HIKARI_HmodelFormat.h"
#include "Assets/Models/Loading/HIKARI_ModelSourceLoader.h"
#include "Assets/Importers/Policy/HIKARI_TextureImportPolicy.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Core/Hashing/HIKARI_Sha256.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Projects/HIKARI_ProjectCreationService.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettingsJson.h"
#include "Runtime/HIKARI_RuntimeLaunchConfig.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"

namespace HIKARI::REFERENCE {
namespace {
namespace fs = std::filesystem;
namespace JsonFile = SERIALIZATION::JSON;
const std::array<std::string_view, 6> kModels{
    "FlightHelmet",      "WaterBottle", "MetalRoughSpheres", "NormalTangentTest", "NormalTangentMirrorTest",
    "AlphaBlendModeTest"};

std::string FileHash(const fs::path& path) {
    HASH::Sha256Digest digest;
    std::string error;
    Require(HASH::ComputeFileSha256(path, digest, error), error);
    return HASH::ToHex(digest);
}

fs::path Child(const fs::path& root, const fs::path& relative) {
    Require(!relative.empty() && !relative.is_absolute() && !relative.has_root_path(), "Expected relative asset path");
    const auto candidate = fs::weakly_canonical(root / relative);
    const auto local = candidate.lexically_relative(fs::weakly_canonical(root));
    Require(!local.empty() && *local.begin() != "..", "Asset path escaped root");
    return candidate;
}

void WriteJson(const fs::path& path, const nlohmann::json& json) {
    std::string error;
    Require(JsonFile::WriteJsonFile(path, json, &error), error);
}

nlohmann::json CopyAssets(const fs::path& root, const fs::path& pack) {
    nlohmann::json manifest;
    Require(JsonFile::ReadJsonFile(pack / "asset-manifest.json", manifest), "Missing asset pack manifest");
    Require(manifest.at("assets").size() == kModels.size(), "Expected six reference models");
    std::set<std::string> names;
    for (auto& model : manifest.at("assets")) {
        const auto name = model.at("name").get<std::string>();
        Require(std::find(kModels.begin(), kModels.end(), name) != kModels.end() && names.insert(name).second,
                "Unexpected or duplicate reference model");
        for (auto& file : model.at("files")) {
            const fs::path relative = TEXT::PathFromUtf8(file.at("path").get<std::string>());
            const auto source = Child(pack, relative);
            Require(fs::is_regular_file(source) && fs::file_size(source) == file.at("bytes").get<uint64_t>() &&
                        FileHash(source) == file.at("sha256").get<std::string>(),
                    "Asset source hash mismatch");
            const fs::path base = (relative.filename() == "README.md" || relative.filename() == "metadata.json")
                                      ? "ThirdParty/Khronos"
                                      : "Assets/Models";
            const fs::path destination = base / relative;
            fs::create_directories((root / destination).parent_path());
            Require(fs::copy_file(source, root / destination), "Asset copy failed");
            file["projectPath"] = destination.generic_string();
        }
    }
    manifest["storage"] = "Project-owned validation source assets; retain original author/license metadata";
    WriteJson(root / "ThirdParty/asset-manifest.json", manifest);
    return manifest;
}

void StudioCube(const fs::path& path) {
    DirectX::ScratchImage cube;
    constexpr uint32_t size = 128;
    Require(SUCCEEDED(cube.InitializeCube(DXGI_FORMAT_R32G32B32A32_FLOAT, size, size, 1, 1)),
            "Studio cube allocation failed");
    for (unsigned face = 0; face < 6; ++face) {
        const auto* image = cube.GetImage(0, face, 0);
        for (unsigned y = 0; y < size; ++y) {
            auto* row = reinterpret_cast<float*>(image->pixels + y * image->rowPitch);
            for (unsigned x = 0; x < size; ++x) {
                const float u = 2.0f * (x + 0.5f) / size - 1.0f;
                const float v = 2.0f * (y + 0.5f) / size - 1.0f;
                const MATH::Vec3 faces[]{{1, -v, -u}, {-1, -v, u}, {u, 1, v}, {u, -1, -v}, {u, -v, 1}, {-u, -v, -1}};
                const auto direction = MATH::Normalize(faces[face]);
                // 固定された解析的な照明パネル。外部 HDRI の変更に依存しない。
                const float key =
                    std::pow((std::max)(0.0f, MATH::Dot(direction, MATH::Normalize({-0.7f, 0.8f, 0.6f}))), 48.0f) *
                    6.0f;
                const float rim =
                    std::pow((std::max)(0.0f, MATH::Dot(direction, MATH::Normalize({0.8f, 0.3f, -0.5f}))), 96.0f) *
                    4.0f;
                const float base = direction.y >= 0 ? 0.18f : 0.06f;
                row[x * 4] = base + key + rim * 0.8f;
                row[x * 4 + 1] = base + key * 0.95f + rim * 0.9f;
                row[x * 4 + 2] = base + key * 0.9f + rim;
                row[x * 4 + 3] = 1.0f;
            }
        }
    }
    fs::create_directories(path.parent_path());
    Require(SUCCEEDED(DirectX::SaveToDDSFile(cube.GetImages(), cube.GetImageCount(), cube.GetMetadata(),
                                             DirectX::DDS_FLAGS_NONE, path.c_str())),
            "Studio cube save failed");
}

void AuthorTextureRoles(const fs::path& root, AssetDatabase& database) {
    for (const auto name : kModels) {
        ModelAsset source;
        source.SetSourcePath(TEXT::PathToUtf8(root / "Assets/Models" / name / "glTF" / (std::string(name) + ".gltf")));
        Require(ASSETS::MODELS::LoadModelSource(source), "Could not inspect source material slots");
        for (const auto& material : source.materials) {
            const std::pair<TextureSlot, TextureUsage> slots[]{
                {material.occlusionTexture, TextureUsage::MetallicRoughness},
                {material.metallicRoughnessTexture, TextureUsage::MetallicRoughness},
                {material.baseColorTexture, TextureUsage::BaseColor},
                {material.emissiveTexture, TextureUsage::Emissive},
                {material.normalTexture, TextureUsage::Normal}};
            for (const auto& [slot, usage] : slots) {
                if (slot.textureIndex < 0)
                    continue;
                Require(static_cast<size_t>(slot.textureIndex) < source.textures.size(), "Invalid source texture slot");
                const auto path = fs::relative(TEXT::PathFromUtf8(source.textures[slot.textureIndex].sourcePath), root);
                AssetRecord record;
                record.sourcePath = path;
                record.metaPath = database.GetMetaPathForSource(path);
                Require(database.ReadMeta(record.metaPath, record.meta), "Missing authored texture metadata");
                auto settings =
                    ASSETS::IMPORT_POLICY::ResolveTextureImportSettings(path, record.meta.importSettingsJson);
                // サンプル名の文字列推測ではなく、実際の材質スロットを入力条件に固定する。
                settings.usage = usage;
                settings.colorSpace = usage == TextureUsage::BaseColor || usage == TextureUsage::Emissive
                                          ? TextureAssetColorSpace::Srgb
                                          : TextureAssetColorSpace::Linear;
                record.meta.importSettingsJson = ASSETS::IMPORT_POLICY::MakeTextureImportSettingsJson(settings);
                Require(database.WriteMeta(record), "Could not author texture role");
            }
        }
    }
}

std::vector<ReferenceModel> ImportSources(const fs::path& root, AssetDatabase& database) {
    std::vector<fs::path> paths;
    for (const auto& entry : fs::recursive_directory_iterator(root / "Assets")) {
        if (entry.is_regular_file())
            paths.push_back(fs::relative(entry.path(), root));
    }
    std::sort(paths.begin(), paths.end());
    for (const auto& path : paths)
        WriteReferenceMeta(database, path);
    AuthorTextureRoles(root, database);
    Require(database.ScanAssets(false), "Asset scan failed");
    // テクスチャを先に取り込み、モデル importer に既存の依存解決を任せる。
    for (const auto& path : paths) {
        if (path.extension() == ".gltf")
            continue;
        if (const auto* record = database.FindByPath(path)) {
            if (record->type == AssetType::Unknown)
                continue;
            const auto guid = record->guid;
            std::cout << "Import " << path.generic_string() << std::endl;
            const bool imported = database.ImportAsset(guid);
            Require(imported,
                    "Import failed: " + path.generic_string() + ": " + database.FindByGuid(guid)->lastImportMessage);
        }
    }
    std::vector<ReferenceModel> result;
    for (const auto name : kModels) {
        const fs::path path = "Assets/Models/" + std::string(name) + "/glTF/" + std::string(name) + ".gltf";
        const auto guid = ReferenceGuid(path);
        std::cout << "Import " << name << std::endl;
        Require(database.ImportAsset(guid), "Model import failed: " + std::string(name));
        ModelAsset model;
        std::string error;
        Require(ReadHmodelFile(database.GetImportedDirectory(guid) / "model.hmodel", model, error), error);
        Require(!model.meshes.empty() && !model.materials.empty(), "Imported model is empty");
        result.push_back({std::string(name), guid, model.bounds});
    }
    return result;
}
} // namespace

AssetGuid ReferenceGuid(const fs::path& relativePath) {
    HASH::Sha256Digest digest;
    Require(HASH::ComputeSha256("HIKARI.RenderValidation.v1/" + relativePath.generic_string(), digest),
            "GUID hash failed");
    // AssetGuid の正規形式は区切りなしの 32 桁。同一入力で固定する。
    return {HASH::ToHex(digest).substr(0, 32)};
}

void WriteReferenceMeta(AssetDatabase& database, const fs::path& source) {
    auto* importer = database.GetImporterRegistry().FindForSource(source);
    if (!importer)
        return;
    AssetRecord record;
    record.sourcePath = source;
    record.metaPath = database.GetMetaPathForSource(source);
    record.meta = importer->CreateDefaultMeta(source, ReferenceGuid(source));
    if (source.extension() == ".gltf") {
        auto settings = nlohmann::json::parse(record.meta.importSettingsJson);
        settings["lightmapUv"] = {{"mode", "Preserve"}};
        record.meta.importSettingsJson = settings.dump(2);
    }
    if (source.extension() == ".dds") {
        auto settings = nlohmann::json::parse(record.meta.importSettingsJson);
        settings["irradianceSize"] = 32;
        settings["prefilteredSize"] = 128;
        settings["prefilteredMipCount"] = 8;
        settings["prefilteredSampleCount"] = 256;
        record.meta.importSettingsJson = settings.dump(2);
    }
    Require(database.WriteMeta(record), "Metadata write failed: " + source.generic_string());
}

void VerifyValidationProject(const fs::path& root) {
    nlohmann::json suite, sourceManifest;
    Require(JsonFile::ReadJsonFile(root / "ValidationSuite.json", suite), "Missing validation suite");
    Require(JsonFile::ReadJsonFile(root / "ThirdParty/asset-manifest.json", sourceManifest), "Missing source manifest");
    for (const auto& model : sourceManifest.at("assets")) {
        for (const auto& file : model.at("files")) {
            Require(FileHash(Child(root, TEXT::PathFromUtf8(file.at("projectPath").get<std::string>()))) ==
                        file.at("sha256").get<std::string>(),
                    "Project source asset changed");
        }
    }
    AssetDatabase database;
    Require(database.Initialize(root) && database.ScanAssets(false), "Validation asset scan failed");
    ProjectSettingsService settings;
    Require(settings.Load(root) &&
                settings.GetSettings().startupSceneGuid.value == suite.at("scenes").at(0).at("guid").get<std::string>(),
            "Startup scene is not the material studio");
    nlohmann::json report = {{"version", 1}, {"gpuRendered", false}, {"scenes", nlohmann::json::array()}};
    for (const auto& entry : suite.at("scenes")) {
        const auto profile = LoadRuntimeLaunchConfigFromFile(
            Child(root, "ProjectSettings/ValidationProfiles/" + entry.at("name").get<std::string>() + ".json"));
        Require(profile.loaded && fs::weakly_canonical(profile.projectRoot) == fs::weakly_canonical(root) &&
                    profile.startupSceneGuid == entry.at("guid").get<std::string>(),
                "Scene launch profile does not match project");
        Require(profile.windowWidth == 1920 && profile.windowHeight == 1080 && profile.renderQuality.has_value(),
                "Missing fixed render profile");
        const auto& quality = *profile.renderQuality;
        Require(quality.windowSize == RENDER3D::RenderResolutionPreset::P1080 &&
                    quality.sceneResolution == RENDER3D::RenderResolutionPreset::P1080 &&
                    quality.viewportScale == 1.0f &&
                    quality.antiAliasingMode == RENDER3D::RenderAntiAliasingMode::TAA &&
                    quality.frameGenerationMode == RENDER3D::RenderFrameGenerationMode::Off && !quality.vSync,
                "Render profile no longer matches suite conditions");
        SceneDocument scene;
        Require(SceneSerializer{}.LoadFromFile(
                    TEXT::PathToUtf8(Child(root, TEXT::PathFromUtf8(entry.at("path").get<std::string>()))), scene),
                "Scene round-trip failed");
        Require(scene.objects.size() == entry.at("objectCount").get<size_t>(), "Scene object count changed");
        Require(scene.camera.defaultCameraObjectId.has_value(), "Missing default camera");
        std::set<uint64_t> ids;
        size_t modelCount = 0;
        for (const auto& object : scene.objects) {
            Require(object.id.value && ids.insert(object.id.value).second, "Duplicate object identity");
            for (const auto& component : object.components) {
                for (const auto* key : {"assetId", "materialSelection"}) {
                    if (!component.properties.contains(key))
                        continue;
                    const AssetGuid guid{component.properties.at(key).get<std::string>()};
                    Require(database.FindByGuid(guid) != nullptr, "Unresolved scene asset: " + guid.value);
                }
                if (component.type == "ModelComponent")
                    ++modelCount;
            }
        }
        Require(ids.contains(scene.camera.defaultCameraObjectId->value), "Default camera object missing");
        Require(modelCount == entry.at("modelInstanceCount").get<size_t>(), "Model instance count changed");
        report["scenes"].push_back({{"name", scene.sceneName}, {"objects", ids.size()}, {"models", modelCount}});
    }
    for (const auto* record : database.CollectAll()) {
        if (record->type == AssetType::Unknown)
            continue;
        Require(IsValidAssetGuid(record->guid.value) && !record->duplicateGuid && !record->importerMissing &&
                    !record->artifactMissing && !record->importOutdated && record->sourceExists,
                "Asset not ready: " + record->sourcePath.generic_string() + ": " + record->lastImportMessage);
        if (record->type == AssetType::Model) {
            ModelAsset cooked;
            std::string error;
            Require(ReadHmodelFile(database.GetImportedDirectory(record->guid) / "model.hmodel", cooked, error), error);
            Require(!cooked.meshes.empty() && !cooked.materials.empty() &&
                        cooked.importDiagnostics.unresolvedTextureCount == 0,
                    "Cooked model has missing geometry/material/texture");
        }
        for (const auto& artifact : record->artifactManifest.artifacts) {
            Require(fs::is_regular_file(Child(root, TEXT::PathFromUtf8(artifact.path))), "Missing cooked artifact");
        }
    }
    const auto* sky = database.FindByGuid(ReferenceGuid("Assets/Skies/ReferenceStudio.dds"));
    Require(sky != nullptr, "Missing studio sky");
    for (const auto kind :
         {ASSETS::SEMANTICS::AssetArtifactKind::IblIrradiance, ASSETS::SEMANTICS::AssetArtifactKind::IblPrefiltered,
          ASSETS::SEMANTICS::AssetArtifactKind::BrdfLut}) {
        Require(ASSETS::SEMANTICS::HasAssetArtifact(*sky, kind), "Missing studio IBL artifact");
    }
    Require(FileHash(root / "Assets/Skies/ReferenceStudio.dds") == suite.at("studioSourceSha256").get<std::string>(),
            "Studio lighting input changed");
    report["sourceHashesVerified"] = true;
    report["launchProfilesVerified"] = true;
    report["assetCount"] = database.CollectAll().size();
    fs::create_directories(root / "Library/Validation");
    WriteJson(root / "Library/Validation/project-check.json", report);
    std::cout << "PASS: " << report.dump() << std::endl;
}

void CreateValidationProject(const fs::path& destination, const fs::path& assetPack) {
    const auto output = fs::absolute(destination).lexically_normal();
    Require(!fs::exists(output), "Project destination already exists");
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Require(SUCCEEDED(com), "COM initialization failed");
    struct ComScope {
        ~ComScope() { CoUninitialize(); }
    } comScope;
    // importer 自身の一時パスにも余裕を残す短い作業名を使う。
    const auto stagingParent = output.parent_path() / (".rv-" + std::to_string(GetCurrentProcessId()));
    Require(fs::create_directory(stagingParent), "Could not create staging directory");
    EDITOR::ProjectCreationResult project;
    std::string error;
    Require(EDITOR::CreateProject({stagingParent, TEXT::PathToUtf8(output.filename())}, project, error), error);
    const auto root = project.projectRoot;
    std::cout << "Preparing " << TEXT::PathToUtf8(root) << std::endl;
    const auto sourceManifest = CopyAssets(root, fs::absolute(assetPack));
    StudioCube(root / "Assets/Skies/ReferenceStudio.dds");
    nlohmann::json scenes;
    {
        AssetDatabase database;
        Require(database.Initialize(root), "Project asset database initialization failed");
        const auto models = ImportSources(root, database);
        scenes = CreateValidationScenes(root, database, models);
        Require(database.ScanAssets(false), "Scene asset scan failed");
        const auto imported = database.ImportAllOutdated();
        Require(imported.failed == 0, "Suite dependency import failed");
    }
    ProjectSettingsService settings;
    Require(settings.Load(root), "Project settings load failed");
    Require(settings.SetStartupSceneGuid({scenes.at(0).at("guid").get<std::string>()}), "Invalid startup scene GUID");
    Require(settings.Save(), "Project settings save failed");
    RENDER3D::RenderQualitySettings quality;
    quality.windowSize = quality.sceneResolution = RENDER3D::RenderResolutionPreset::P1080;
    quality.antiAliasingMode = RENDER3D::RenderAntiAliasingMode::TAA;
    quality.frameGenerationMode = RENDER3D::RenderFrameGenerationMode::Off;
    // 最も広い Stress_10000（影の距離 600 m）まで覆う影の遠平面。
    quality.directionalShadow.farPlane = 800.0f;
    WriteJson(root / "runtime_config.json", {{"runtime",
                                              {{"enableDebugLayer", false},
                                               {"enableDebugCamera", false},
                                               {"windowWidth", 1920},
                                               {"windowHeight", 1080},
                                               {"renderQuality", RENDER3D::SerializeRenderQualitySettings(quality)}}}});
    nlohmann::json runtimeConfig;
    Require(JsonFile::ReadJsonFile(root / "runtime_config.json", runtimeConfig), "Missing runtime config");
    fs::create_directories(root / "ProjectSettings/ValidationProfiles");
    for (const auto& scene : scenes) {
        runtimeConfig["runtime"]["projectRoot"] = "../..";
        runtimeConfig["runtime"]["startupSceneGuid"] = scene.at("guid");
        WriteJson(root / "ProjectSettings/ValidationProfiles" / (scene.at("name").get<std::string>() + ".json"),
                  runtimeConfig);
    }
    WriteJson(root / "ValidationSuite.json",
              {{"version", 1},
               {"scenes", scenes},
               {"sourceRevision", sourceManifest.at("revision")},
               {"studioSourceSha256", FileHash(root / "Assets/Skies/ReferenceStudio.dds")},
               {"benchmark", {{"warmupFrames", 180}, {"sampleFrames", 600}, {"repeats", 3}}},
               {"knownUnsupported",
                {"FlightHelmet KHR_materials_transmission", "Negative-scale raster winding remains under review"}}});
    VerifyValidationProject(root);
    // 完成済みの新規ディレクトリだけを公開する。既存プロジェクトは触らない。
    Require(root.parent_path() == stagingParent && output.parent_path() == stagingParent.parent_path() &&
                !fs::exists(output),
            "Project publish boundary changed");
    fs::rename(root, output);
    fs::remove(stagingParent);
    VerifyValidationProject(output);
    std::cout << "Created " << TEXT::PathToUtf8(output / "Project.hikari") << std::endl;
}
} // namespace HIKARI::REFERENCE
