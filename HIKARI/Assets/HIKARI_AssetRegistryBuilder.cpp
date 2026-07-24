#include "HIKARI_AssetRegistryBuilder.h"

#include <filesystem>
#include <memory>
#include <system_error>

#include <json.hpp>

#include "Assets/Formats/HIKARI_HmatFormat.h"
#include "Assets/Importers/Policy/HIKARI_ModelImportPolicy.h"
#include "Assets/Importers/Policy/HIKARI_TextureImportPolicy.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Core/HIKARI_Logger.h"

namespace HIKARI {

    namespace {

        bool LoadMaterialSourceJson(
            const AssetDatabase& assetDatabase,
            const AssetRecord& record,
            PbrMaterialAssetData& outData) {

            const std::filesystem::path sourcePath =
                (assetDatabase.GetProjectRoot() / record.sourcePath).lexically_normal();
            std::string loadError{};
            if (LoadPbrMaterialAssetData(sourcePath, outData, loadError)) {
                return true;
            }

            HIKARI_LOG_WARN("[AssetRegistryBuilder][Material] source JSON read failed: " + loadError);
            return false;
        }

        bool LoadMaterialDataForRegistry(
            const AssetDatabase& assetDatabase,
            const AssetRecord& record,
            PbrMaterialAssetData& outData) {

            // Cook 済み HMAT を優先し、無い場合だけ source JSON を読む。
            const std::string hmatArtifact =
                ASSETS::SEMANTICS::FindAssetArtifactPath(
                    record,
                    ASSETS::SEMANTICS::AssetArtifactKind::Material);
            if (!hmatArtifact.empty()) {
                const std::filesystem::path hmatPath =
                    (assetDatabase.GetProjectRoot() / hmatArtifact).lexically_normal();

                std::string readMessage{};
                if (ReadHmatFile(hmatPath, outData, readMessage)) {
                    HIKARI_LOG_INFO("[AssetRegistryBuilder][Material] loaded HMAT: " + hmatArtifact);
                    return true;
                }

                HIKARI_LOG_WARN("[AssetRegistryBuilder][Material][WARN] HMAT read failed: " + readMessage);
                return LoadMaterialSourceJson(assetDatabase, record, outData);
            }

            // 旧 asset 互換のため JSON 読み込み経路は残す。
            HIKARI_LOG_WARN("[AssetRegistryBuilder][Material] HMAT artifact missing, fallback to source JSON: " +
                record.sourcePath.generic_string());
            return LoadMaterialSourceJson(assetDatabase, record, outData);
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

    }

    bool AssetRegistryBuilder::AppendToRegistry(const AssetDatabase& assetDatabase, AssetRegistry& registry) const {
        bool ok = true;

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

                if (!LoadMaterialDataForRegistry(assetDatabase, *record, descriptor->data)) {
                    descriptor->data.materialName = record->displayName.empty()
                        ? record->sourcePath.stem().string()
                        : record->displayName;
                    HIKARI_LOG_WARN("[AssetRegistryBuilder][Material] using fallback material data: " +
                        descriptor->data.materialName);
                }
                ok = registry.RegisterDescriptor(std::move(descriptor)) && ok;
            } else if (record->type == AssetType::VfxEffect) {
                auto descriptor = std::make_unique<VfxAssetDescriptor>();
                descriptor->id.value = record->guid.value;
                descriptor->type = AssetType::VfxEffect;
                descriptor->sourcePath = record->sourcePath.generic_string();
                descriptor->version = record->meta.importerVersion;
                ok = registry.RegisterDescriptor(std::move(descriptor)) && ok;
            }
        }

        return ok;
    }

} // namespace HIKARI
