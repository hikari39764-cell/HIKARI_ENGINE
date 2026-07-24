#include "HIKARI_MaterialImporter.h"

#include "Assets/Formats/HIKARI_HmatFormat.h"
#include "Assets/Importers/Policy/HIKARI_MaterialImportPolicy.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Core/HIKARI_Logger.h"
#include "Project/Paths/HIKARI_ProjectPath.h"

namespace HIKARI {

    namespace {

        void AddTextureDependency(
            std::string role,
            const MaterialTextureSlotData& slot,
            AssetImportResult& result) {

            if (!slot.useTexture || !slot.textureAssetGuid.IsValid()) {
                return;
            }

            result.dependencies.push_back(AssetDependencyDesc{
                slot.textureAssetGuid,
                {},
                std::move(role)
            });
        }
    }

    ASSETS::SEMANTICS::AssetImporterKind
        MaterialImporter::GetImporterKind() const noexcept {
        return ASSETS::SEMANTICS::AssetImporterKind::Material;
    }

    AssetMeta MaterialImporter::CreateDefaultMeta(
        const std::filesystem::path& sourcePath,
        const AssetGuid& guid) const {

        AssetMeta meta = ASSETS::SEMANTICS::MakeBaseAssetMeta(
            sourcePath,
            guid,
            GetImporterKind());
        meta.displayName = sourcePath.stem().stem().string();
        meta.importSettingsJson =
            ASSETS::IMPORT_POLICY::
                MakeDefaultMaterialImportSettingsJson();
        return meta;
    }

    AssetImportResult MaterialImporter::Import(
        const AssetRecord& record,
        const AssetImportContext& context) {

        AssetImportResult result{};
        if (!ASSETS::SEMANTICS::IsAssetSourceForImporter(
            record.sourcePath,
            GetImporterKind())) {
            result.message = "[AssetImporter] unsupported material source: " + record.sourcePath.generic_string();
            HIKARI_LOG_WARN("[MaterialImporter] unsupported material source: " + record.sourcePath.generic_string());
            return result;
        }

        const std::filesystem::path sourcePath =
            (context.projectRoot / record.sourcePath).lexically_normal();

        PbrMaterialAssetData data{};
        std::string error{};
        if (!LoadPbrMaterialAssetData(sourcePath, data, error)) {
            result.message = "[MaterialImporter][ERROR] Material JSON validation failed. " + error;
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        // Material JSON を検証し、必要なら runtime 用 HMAT へ cook する。
        AddTextureDependency("BaseColor", data.baseColorTexture, result);
        AddTextureDependency("Normal", data.normalTexture, result);
        AddTextureDependency("MetallicRoughness", data.metallicRoughnessTexture, result);
        AddTextureDependency("Occlusion", data.occlusionTexture, result);
        AddTextureDependency("Emissive", data.emissiveTexture, result);
        AddTextureDependency("Specular", data.specularTexture, result);
        AddTextureDependency("SpecularColor", data.specularColorTexture, result);

        const ASSETS::IMPORT_POLICY::MaterialImportPolicy importPolicy =
            ASSETS::IMPORT_POLICY::ResolveMaterialImportPolicy(
                record.meta.importSettingsJson);
        if (!importPolicy.ShouldCookHmat()) {
            result.success = true;
            result.message = "[MaterialImporter] Material JSON validated without cook: " + record.sourcePath.generic_string();
            HIKARI_LOG_INFO("[MaterialImporter] validate only: " + record.sourcePath.generic_string());
            return result;
        }

        const std::filesystem::path outputPath =
            (context.importedDirectory / "material.hmat").lexically_normal();

        std::string writeMessage{};
        if (!WriteHmatFile(outputPath, data, writeMessage)) {
            result.message = "[MaterialImporter][ERROR] HMAT write failed: " + writeMessage;
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        const std::filesystem::path relativeOutput =
            PROJECT_PATHS::MakeProjectRelativePath(context.projectRoot, outputPath);
        result.artifacts.push_back(
            ASSETS::SEMANTICS::MakeAssetArtifact(
                ASSETS::SEMANTICS::AssetArtifactKind::Material,
                relativeOutput.generic_string()));

        result.success = true;
        result.message = "[MaterialImporter] Material cooked HMAT: " + relativeOutput.generic_string();
        HIKARI_LOG_INFO("[MaterialImporter] cooked HMAT: " + relativeOutput.generic_string());
        return result;
    }

} // namespace HIKARI
