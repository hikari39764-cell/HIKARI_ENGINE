#include "HIKARI_AssetRegistryBuilder.h"

#include <filesystem>
#include <memory>
#include <system_error>

#include <json.hpp>

#include "Assets/Formats/HIKARI_HmatFormat.h"
#include "Assets/Formats/HIKARI_HvfxcFormat.h"
#include "Assets/Importers/Policy/HIKARI_ModelImportPolicy.h"
#include "Assets/Importers/Policy/HIKARI_TextureImportPolicy.h"
#include "Assets/Material/Compiler/HIKARI_MaterialCompiler.h"
#include "Assets/Material/Authoring/HIKARI_MaterialInstanceResolver.h"
#include "Assets/Material/HIKARI_MaterialBuiltInAssets.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Assets/Material/HIKARI_MaterialAssetLookup.h"
#include "Assets/Models/MaterialBindings/HIKARI_ModelMaterialBindingAsset.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"

namespace HIKARI {

    namespace {

        bool LoadMaterialSourceJson(
            const AssetDatabase& assetDatabase,
            const AssetRecord& record,
            MaterialAssetData& outData,
            MaterialCompileResult& outCompileResult) {

            const std::filesystem::path sourcePath =
                (assetDatabase.GetProjectRoot() / record.sourcePath).lexically_normal();
            std::string loadError{};
            if (LoadMaterialAssetData(sourcePath, outData, loadError)) {
                outCompileResult = CompileMaterialAsset(
                    outData,
                    ASSETS::MATERIAL::MakeMaterialCompileOptions(
                        assetDatabase));
                return true;
            }

            HIKARI_LOG_WARN("[AssetRegistryBuilder][Material] source JSON read failed: " + loadError);
            return false;
        }

        bool LoadRawMaterialDataForRegistry(
            const AssetDatabase& assetDatabase,
            const AssetRecord& record,
            MaterialAssetData& outData,
            MaterialCompileResult& outCompileResult) {

            // Cook 済み HMAT を優先し、無い場合だけ source JSON を読む。
            const std::string hmatArtifact =
                ASSETS::SEMANTICS::FindAssetArtifactPath(
                    record,
                    ASSETS::SEMANTICS::AssetArtifactKind::Material);
            if (!hmatArtifact.empty()) {
                const std::filesystem::path hmatPath =
                    (assetDatabase.GetProjectRoot() / hmatArtifact).lexically_normal();

                std::string readMessage{};
                if (ReadHmatFile(
                        hmatPath,
                        outData,
                        outCompileResult,
                        ASSETS::MATERIAL::MakeMaterialCompileOptions(
                            assetDatabase),
                        readMessage)) {
                    HIKARI_LOG_INFO("[AssetRegistryBuilder][Material] loaded HMAT: " + hmatArtifact);
                    return true;
                }

                HIKARI_LOG_WARN("[AssetRegistryBuilder][Material][WARN] HMAT read failed: " + readMessage);
                return LoadMaterialSourceJson(
                    assetDatabase, record, outData, outCompileResult);
            }

            // 旧 asset 互換のため JSON 読み込み経路は残す。
            HIKARI_LOG_WARN("[AssetRegistryBuilder][Material] HMAT artifact missing, fallback to source JSON: " +
                record.sourcePath.generic_string());
            return LoadMaterialSourceJson(
                assetDatabase, record, outData, outCompileResult);
        }

        bool LoadMaterialDataForRegistry(
            const AssetDatabase& assetDatabase,
            const AssetRecord& record,
            MaterialAssetData& outData,
            MaterialCompileResult& outCompileResult) {

            MaterialAssetData source{};
            MaterialCompileResult sourceCompile{};
            if (!LoadRawMaterialDataForRegistry(
                    assetDatabase, record, source, sourceCompile)) {
                return false;
            }
            if (source.kind != MaterialAssetKind::Instance) {
                outData = std::move(source);
                outCompileResult = std::move(sourceCompile);
                return true;
            }

            const ASSETS::MATERIAL::MaterialInstanceResolveResult resolved =
                ASSETS::MATERIAL::ResolveMaterialInstance(
                    source,
                    [&assetDatabase](
                        const AssetGuid& guid,
                        MaterialAssetData& outParent,
                        std::string& outError) {
                        const AssetRecord* parent =
                            assetDatabase.FindByGuid(guid);
                        if (parent == nullptr ||
                            parent->type != AssetType::Material) {
                            outError =
                                "parent Material asset record was not found";
                            return false;
                        }
                        MaterialCompileResult ignoredCompile{};
                        if (!LoadRawMaterialDataForRegistry(
                                assetDatabase, *parent,
                                outParent, ignoredCompile)) {
                            outError =
                                "parent Material asset data could not be loaded";
                            return false;
                        }
                        outError.clear();
                        return true;
                    },
                    16u,
                    ASSETS::MATERIAL::MakeMaterialCompileOptions(
                        assetDatabase));
            if (!resolved.success) {
                HIKARI_LOG_WARN(
                    "[AssetRegistryBuilder][Material] Material Instance "
                    "composition failed: " + record.guid.value);
                return false;
            }
            outData = resolved.resolvedMaterial;
            outCompileResult = resolved.compile;
            return true;
        }

        std::string FindSharedBrdfLutPath(const AssetDatabase& assetDatabase) {
            const std::filesystem::path relativePath =
                std::filesystem::path("Library") / "Generated" / "IBL" / "brdf_lut.dds";
            const std::filesystem::path absolutePath =
                (assetDatabase.GetProjectRoot() / relativePath).lexically_normal();

            std::error_code ec{};
            if (std::filesystem::exists(absolutePath, ec) && !ec) {
                return relativePath.generic_string();
            }

            return {};
        }

        bool AppendEngineContentTextureManifest(
            AssetRegistry& registry,
            const CONTENT::EngineContentContext& content,
            const char* relativeManifestPath,
            const char* domainLabel) {
            const std::filesystem::path manifestPath =
                content.Resolve(relativeManifestPath);
            nlohmann::json root{};
            std::string readError{};
            if (manifestPath.empty() ||
                !SERIALIZATION::JSON::ReadJsonFile(
                    manifestPath, root, &readError) ||
                !root.is_object() ||
                root.value("schema", std::string{}) !=
                    "hikari.engine_content_assets" ||
                root.value("version", 0u) != 1u ||
                !root.contains("assets") ||
                !root["assets"].is_array()) {
                HIKARI_LOG_ERROR(
                    std::string(
                        "[AssetRegistryBuilder][EngineContent] invalid ") +
                    domainLabel + " asset catalog: " + readError);
                return false;
            }

            bool ok = true;
            for (const nlohmann::json& asset : root["assets"]) {
                if (!asset.is_object() ||
                    asset.value("type", std::string{}) != "Texture") {
                    HIKARI_LOG_ERROR(
                        std::string(
                            "[AssetRegistryBuilder][EngineContent] "
                            "unsupported ") +
                        domainLabel + " asset catalog entry");
                    ok = false;
                    continue;
                }

                const std::string id = asset.value("id", std::string{});
                const std::filesystem::path sourcePath = content.Resolve(
                    asset.value("path", std::string{}));
                std::error_code ec{};
                if (id.empty() || sourcePath.empty() ||
                    !std::filesystem::is_regular_file(sourcePath, ec) || ec) {
                    HIKARI_LOG_ERROR(
                        std::string(
                            "[AssetRegistryBuilder][EngineContent] ") +
                        domainLabel + " texture is missing: " + id);
                    ok = false;
                    continue;
                }

                auto descriptor =
                    std::make_unique<TextureAssetDescriptor>();
                descriptor->id.value = id;
                descriptor->type = AssetType::Texture;
                descriptor->sourcePath = sourcePath.generic_string();
                descriptor->version = root.value("version", 1u);
                descriptor->dimension =
                    TextureAssetDimension::Texture2D;
                descriptor->colorSpace =
                    ASSETS::IMPORT_POLICY::ParseTextureColorSpace(
                        asset.value("colorSpace", std::string{}),
                        TextureAssetColorSpace::Srgb);
                descriptor->usage =
                    ASSETS::IMPORT_POLICY::ParseTextureUsage(
                        asset.value("usage", std::string{}),
                        TextureUsage::BaseColor);
                descriptor->compression =
                    ASSETS::IMPORT_POLICY::ParseTextureCompression(
                        asset.value("compression", std::string{}),
                        TextureCompression::None);
                descriptor->mipPolicy =
                    ASSETS::IMPORT_POLICY::ParseTextureMipPolicy(
                        asset.value("mipPolicy", std::string{}),
                        TextureMipPolicy::Preserve);
                ok = registry.RegisterDescriptor(
                    std::move(descriptor)) && ok;
            }
            return ok;
        }

        bool AppendEngineContentTextures(AssetRegistry& registry) {
            const CONTENT::EngineContentContext& content =
                CONTENT::GetEngineContentContext();
            if (!content.IsOpen()) {
                return true;
            }

            bool ok = AppendEngineContentTextureManifest(
                registry,
                content,
                "Vfx/BuiltinAssets.json",
                "VFX");
            ok = AppendEngineContentTextureManifest(
                     registry,
                     content,
                     "Sand/BuiltinAssets.json",
                     "Sand") &&
                ok;
            ok = AppendEngineContentTextureManifest(
                     registry,
                     content,
                     "Terrain/BuiltinAssets.json",
                     "Terrain") &&
                ok;
            return ok;
        }

        bool AppendBuiltInMaterials(
            AssetRegistry& registry,
            const AssetDatabase& assetDatabase) {

            // プロシージャルグラフィックスの「デフォルト白」マテリアル。
            // ディスク資産を持たず、AssetGuid 文字列をそのまま ID にする。
            PbrMaterialAssetData white{};
            white.materialName = "Default White";
            white.baseColorFactor = { 1.0f, 1.0f, 1.0f, 1.0f };
            white.metallicFactor = 0.0f;
            white.roughnessFactor = 0.95f;

            auto descriptor = std::make_unique<MaterialAssetDescriptor>();
            descriptor->id.value =
                std::string(ASSETS::MATERIAL::kBuiltInWhiteMaterialAssetId);
            descriptor->type = AssetType::Material;
            descriptor->sourcePath = descriptor->id.value;
            descriptor->version = 1u;
            descriptor->sourceData =
                MakeStandardPbrMaterialAssetData(white);
            descriptor->compileResult = CompileMaterialAsset(
                descriptor->sourceData,
                ASSETS::MATERIAL::MakeMaterialCompileOptions(
                    assetDatabase));
            descriptor->data =
                ProjectPbrMaterialAssetData(descriptor->sourceData);
            bool ok = registry.RegisterDescriptor(std::move(descriptor));

            // チェッカー(デバッグ)用テクスチャ。
            // マテリアル ID とレジストリキーが衝突しないよう
            // テクスチャは専用 ID を使う。
            const std::string checkerTextureId =
                std::string(
                    ASSETS::MATERIAL::kBuiltInCheckerMaterialAssetId) +
                "/texture";
            auto checkerTexture =
                std::make_unique<TextureAssetDescriptor>();
            checkerTexture->id.value = checkerTextureId;
            checkerTexture->type = AssetType::Texture;
            checkerTexture->sourcePath = std::string(
                ASSETS::MATERIAL::kBuiltInCheckerMaterialAssetId);
            checkerTexture->version = 1u;
            checkerTexture->dimension = TextureAssetDimension::Texture2D;
            checkerTexture->colorSpace = TextureAssetColorSpace::Srgb;
            checkerTexture->usage = TextureUsage::BaseColor;
            checkerTexture->compression = TextureCompression::None;
            checkerTexture->mipPolicy = TextureMipPolicy::Preserve;
            ok = registry.RegisterDescriptor(
                std::move(checkerTexture)) && ok;

            // チェッカー(デバッグ)マテリアル。
            PbrMaterialAssetData checker{};
            checker.materialName = "Checker (Debug)";
            checker.baseColorFactor = { 1.0f, 1.0f, 1.0f, 1.0f };
            checker.baseColorTexture.useTexture = true;
            checker.baseColorTexture.textureAssetGuid.value =
                checkerTextureId;
            checker.metallicFactor = 0.0f;
            checker.roughnessFactor = 0.95f;

            auto checkerMaterial =
                std::make_unique<MaterialAssetDescriptor>();
            checkerMaterial->id.value = std::string(
                ASSETS::MATERIAL::kBuiltInCheckerMaterialAssetId);
            checkerMaterial->type = AssetType::Material;
            checkerMaterial->sourcePath = checkerMaterial->id.value;
            checkerMaterial->version = 1u;
            checkerMaterial->sourceData =
                MakeStandardPbrMaterialAssetData(checker);
            checkerMaterial->compileResult = CompileMaterialAsset(
                checkerMaterial->sourceData,
                ASSETS::MATERIAL::MakeMaterialCompileOptions(
                    assetDatabase));
            checkerMaterial->data =
                ProjectPbrMaterialAssetData(
                    checkerMaterial->sourceData);
            ok = registry.RegisterDescriptor(
                std::move(checkerMaterial)) && ok;
            return ok;
        }

        bool AppendBuiltInTerrainMaterials(
            AssetRegistry& registry,
            const AssetDatabase& assetDatabase) {

            PbrMaterialAssetData sand{};
            sand.materialName = "Terrain Surface (Stylized Layers)";
            sand.baseColorFactor = { 1.0f, 1.0f, 1.0f, 1.0f };
            sand.baseColorTexture.useTexture = true;
            sand.baseColorTexture.textureAssetGuid.value =
                std::string(
                    ASSETS::MATERIAL::
                        kBuiltInTerrainLayerAlbedoTextureId);
            // Terrain shader が world-space tiling と atlas rect を所有する。
            // uv1 は coverage 専用のため、マテリアル側では変換しない。
            sand.baseColorTexture.uvScale = { 1.0f, 1.0f };
            sand.normalTexture.useTexture = true;
            sand.normalTexture.textureAssetGuid.value =
                std::string(
                    ASSETS::MATERIAL::
                        kBuiltInTerrainLayerNormalTextureId);
            sand.normalTexture.uvScale = { 1.0f, 1.0f };
            sand.normalScale = 1.0f;
            sand.metallicFactor = 0.0f;
            sand.roughnessFactor = 0.92f;
            sand.doubleSided = true;

            auto descriptor =
                std::make_unique<MaterialAssetDescriptor>();
            descriptor->id.value =
                std::string(ASSETS::MATERIAL::kBuiltInSandMaterialAssetId);
            descriptor->type = AssetType::Material;
            descriptor->sourcePath = descriptor->id.value;
            descriptor->version = 1u;
            descriptor->sourceData =
                MakeStandardPbrMaterialAssetData(sand);
            descriptor->compileResult = CompileMaterialAsset(
                descriptor->sourceData,
                ASSETS::MATERIAL::MakeMaterialCompileOptions(
                    assetDatabase));
            descriptor->data =
                ProjectPbrMaterialAssetData(descriptor->sourceData);
            return registry.RegisterDescriptor(std::move(descriptor));
        }

    }

    bool AssetRegistryBuilder::AppendToRegistry(const AssetDatabase& assetDatabase, AssetRegistry& registry) const {
        bool ok = AppendEngineContentTextures(registry);
        ok = AppendBuiltInMaterials(registry, assetDatabase) && ok;
        ok = AppendBuiltInTerrainMaterials(registry, assetDatabase) && ok;

        for (const AssetRecord* record : assetDatabase.CollectAll()) {
            if (!record || !record->guid.IsValid() || record->duplicateGuid) {
                continue;
            }

            if (record->type == AssetType::Texture) {
                auto descriptor = std::make_unique<TextureAssetDescriptor>();
                descriptor->id.value = record->guid.value;
                descriptor->type = AssetType::Texture;
                descriptor->sourcePath =
                    ASSETS::SEMANTICS::FindAssetArtifactPath(
                        *record,
                        ASSETS::SEMANTICS::AssetArtifactKind::MainTexture,
                        true);
                if (descriptor->sourcePath.empty()) {
                    descriptor->sourcePath = record->sourcePath.generic_string();
                }
                descriptor->version = record->meta.importerVersion;

                const TextureImportSettings settings =
                    ASSETS::IMPORT_POLICY::ResolveTextureImportSettings(
                        record->sourcePath,
                        record->meta.importSettingsJson);
                descriptor->dimension = settings.dimension;
                descriptor->colorSpace = settings.colorSpace;
                descriptor->usage = settings.usage;
                descriptor->compression = settings.compression;
                descriptor->mipPolicy = settings.mipPolicy;
                ok = registry.RegisterDescriptor(std::move(descriptor)) && ok;
            } else if (record->type == AssetType::Model) {
                auto descriptor = std::make_unique<ModelAssetDescriptor>();
                descriptor->id.value = record->guid.value;
                descriptor->type = AssetType::Model;
                descriptor->sourcePath =
                    ASSETS::SEMANTICS::FindAssetArtifactPath(
                        *record,
                        ASSETS::SEMANTICS::AssetArtifactKind::MainModel,
                        true);
                if (descriptor->sourcePath.empty()) {
                    descriptor->sourcePath = record->sourcePath.generic_string();
                }
                descriptor->clusteredGeometryPath =
                    ASSETS::SEMANTICS::FindAssetArtifactPath(
                        *record,
                        ASSETS::SEMANTICS::AssetArtifactKind::ClusteredGeometry);
                descriptor->collisionGeometryPath =
                    ASSETS::SEMANTICS::FindAssetArtifactPath(
                        *record,
                        ASSETS::SEMANTICS::AssetArtifactKind::CollisionGeometry);
                descriptor->version = record->meta.importerVersion;
                const ASSETS::IMPORT_POLICY::ModelImportPolicy policy =
                    ASSETS::IMPORT_POLICY::ResolveModelImportPolicy(
                        record->sourcePath,
                        record->meta.importSettingsJson);
                descriptor->importer = policy.importer;
                descriptor->importOptions.loadMaterials = policy.loadMaterials;
                descriptor->importOptions.loadTextures = policy.loadTextures;
                descriptor->importOptions.clusterGeometry =
                    policy.clusterOptions;
                ok = registry.RegisterDescriptor(std::move(descriptor)) && ok;
            } else if (record->type == AssetType::Sky) {
                auto descriptor = std::make_unique<SkyAssetDescriptor>();
                descriptor->id.value = record->guid.value;
                descriptor->type = AssetType::Sky;
                const std::string skyArtifact =
                    ASSETS::SEMANTICS::FindAssetArtifactPath(
                        *record,
                        ASSETS::SEMANTICS::AssetArtifactKind::SkyCubemap,
                        true);
                if (skyArtifact.empty()) {
                    HIKARI_LOG_WARN("[AssetRegistryBuilder][Sky] missing SkyCubemap artifact, fallback to source: " +
                        record->sourcePath.generic_string());
                    descriptor->sourcePath = record->sourcePath.generic_string();
                } else {
                    descriptor->sourcePath = skyArtifact;
                }
                descriptor->version = record->meta.importerVersion;
                descriptor->textureAssetId.clear();
                descriptor->preferredMode = SkyMode::Cubemap;
                descriptor->irradiancePath =
                    ASSETS::SEMANTICS::FindAssetArtifactPath(
                        *record,
                        ASSETS::SEMANTICS::AssetArtifactKind::IblIrradiance,
                        true);
                descriptor->prefilteredPath =
                    ASSETS::SEMANTICS::FindAssetArtifactPath(
                        *record,
                        ASSETS::SEMANTICS::AssetArtifactKind::IblPrefiltered,
                        true);
                descriptor->brdfLutPath =
                    ASSETS::SEMANTICS::FindAssetArtifactPath(
                        *record,
                        ASSETS::SEMANTICS::AssetArtifactKind::BrdfLut,
                        true);
                if (descriptor->brdfLutPath.empty()) {
                    descriptor->brdfLutPath = FindSharedBrdfLutPath(assetDatabase);
                }
                descriptor->hasIbl =
                    !descriptor->irradiancePath.empty() ||
                    !descriptor->prefilteredPath.empty();

                nlohmann::json settings = nlohmann::json::parse(record->meta.importSettingsJson, nullptr, false);
                if (settings.is_object()) {
                    descriptor->prefilteredMipCount = settings.value("prefilteredMipCount", 7u);
                }
                ok = registry.RegisterDescriptor(std::move(descriptor)) && ok;
            } else if (record->type == AssetType::Material) {
                auto descriptor = std::make_unique<MaterialAssetDescriptor>();
                descriptor->id.value = record->guid.value;
                descriptor->type = AssetType::Material;
                descriptor->sourcePath =
                    ASSETS::SEMANTICS::FindAssetArtifactPath(
                        *record,
                        ASSETS::SEMANTICS::AssetArtifactKind::Material);
                if (descriptor->sourcePath.empty()) {
                    descriptor->sourcePath = record->sourcePath.generic_string();
                }
                descriptor->version = record->meta.importerVersion;

                if (!LoadMaterialDataForRegistry(
                        assetDatabase,
                        *record,
                        descriptor->sourceData,
                        descriptor->compileResult)) {
                    PbrMaterialAssetData fallback{};
                    fallback.materialName = record->displayName.empty()
                        ? record->sourcePath.stem().string()
                        : record->displayName;
                    descriptor->sourceData =
                        MakeStandardPbrMaterialAssetData(fallback);
                    descriptor->compileResult =
                        CompileMaterialAsset(
                            descriptor->sourceData,
                            ASSETS::MATERIAL::MakeMaterialCompileOptions(
                                assetDatabase));
                    HIKARI_LOG_WARN("[AssetRegistryBuilder][Material] using fallback material data: " +
                        fallback.materialName);
                }
                descriptor->data =
                    ProjectPbrMaterialAssetData(descriptor->sourceData);
                ok = registry.RegisterDescriptor(std::move(descriptor)) && ok;
            } else if (record->type ==
                AssetType::ModelMaterialBinding) {
                auto descriptor = std::make_unique<
                    ModelMaterialBindingAssetDescriptor>();
                descriptor->id.value = record->guid.value;
                descriptor->type = AssetType::ModelMaterialBinding;
                descriptor->sourcePath =
                    record->sourcePath.generic_string();
                descriptor->version = record->meta.importerVersion;
                const std::filesystem::path sourcePath =
                    record->sourcePath.is_absolute()
                        ? record->sourcePath
                        : (assetDatabase.GetProjectRoot() /
                            record->sourcePath).lexically_normal();
                std::string loadError{};
                if (!ASSETS::MODELS::
                    LoadModelMaterialBindingAsset(
                        sourcePath, descriptor->data, loadError)) {
                    HIKARI_LOG_WARN(
                        "[AssetRegistryBuilder][ModelMaterialBinding] "
                        "source read failed: " + loadError);
                    ok = false;
                    continue;
                }
                ok = registry.RegisterDescriptor(
                    std::move(descriptor)) && ok;
            } else if (record->type == AssetType::VfxEffect) {
                auto descriptor = std::make_unique<VfxAssetDescriptor>();
                descriptor->id.value = record->guid.value;
                descriptor->type = AssetType::VfxEffect;
                descriptor->sourcePath =
                    ASSETS::SEMANTICS::FindAssetArtifactPath(
                        *record,
                        ASSETS::SEMANTICS::AssetArtifactKind::VfxProgram,
                        true);
                if (descriptor->sourcePath.empty()) {
                    HIKARI_LOG_WARN(
                        "[AssetRegistryBuilder][VFX] compiled HVFX artifact "
                        "is missing; runtime effect skipped until import succeeds: " +
                        record->guid.value);
                    continue;
                }
                descriptor->version = record->meta.importerVersion;

                const std::filesystem::path programPath =
                    (assetDatabase.GetProjectRoot() /
                        descriptor->sourcePath).lexically_normal();
                VFX::VfxProgram program{};
                std::string readError{};
                if (!VFX::ReadHvfxcFile(
                        programPath,
                        program,
                        readError)) {
                    HIKARI_LOG_WARN(
                        "[AssetRegistryBuilder][VFX] compiled HVFX read failed; "
                        "runtime effect skipped until import succeeds: " +
                        record->guid.value + " (" + readError + ")");
                    continue;
                }
                descriptor->program =
                    std::make_shared<const VFX::VfxProgram>(
                        std::move(program));
                ok = registry.RegisterDescriptor(std::move(descriptor)) && ok;
            }
        }

        return ok;
    }

} // namespace HIKARI
