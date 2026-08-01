#include "HIKARI_MaterialImporter.h"

#include "Assets/Formats/HIKARI_HmatFormat.h"
#include "Assets/Importers/Policy/HIKARI_MaterialImportPolicy.h"
#include "Assets/Material/Compiler/HIKARI_MaterialCompiler.h"
#include "Assets/Material/Compiler/HIKARI_MaterialProgramSerialization.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Assets/Material/HIKARI_MaterialDependencies.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Core/HIKARI_Logger.h"
#include "Project/Paths/HIKARI_ProjectPath.h"

namespace HIKARI {

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

        MaterialAssetData data{};
        std::vector<std::string> migrationDiagnostics{};
        std::string error{};
        if (!LoadMaterialAssetData(
                sourcePath, data, error, &migrationDiagnostics)) {
            result.message = "[MaterialImporter][ERROR] Material JSON validation failed. " + error;
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        for (const MaterialAssetDependency& dependency :
            CollectMaterialAssetDependencies(data)) {
            result.dependencies.push_back(AssetDependencyDesc{
                dependency.assetGuid,
                {},
                dependency.role
            });
        }

        const std::filesystem::path outputPath =
            (context.importedDirectory / "material.hmat").lexically_normal();
        MaterialAssetData previousSource{};
        MaterialCompileResult previousCompile{};
        std::string previousMessage{};
        const bool hasPreviousProgram =
            std::filesystem::is_regular_file(outputPath) &&
            ReadHmatFile(
                outputPath,
                previousSource,
                previousCompile,
                previousMessage) &&
            !previousCompile.usedFallback;
        const MaterialCompileResult compileResult = CompileMaterialAsset(
            data,
            hasPreviousProgram ? &previousCompile.program : nullptr);

        std::string compileJson{};
        std::string compileJsonError{};
        if (MATERIAL_SERIALIZATION::SerializeMaterialCompileResult(
                compileResult, compileJson, compileJsonError)) {
            result.diagnosticsJson = std::move(compileJson);
        }

        const ASSETS::IMPORT_POLICY::MaterialImportPolicy importPolicy =
            ASSETS::IMPORT_POLICY::ResolveMaterialImportPolicy(
                record.meta.importSettingsJson);
        if (!importPolicy.ShouldCookHmat()) {
            result.success = true;
            result.message = compileResult.success
                ? "[MaterialImporter] Material v4 validated without cook: " + record.sourcePath.generic_string()
                : "[MaterialImporter][WARN] Material v4 compiled with last-good/fallback without cook: " +
                    record.sourcePath.generic_string();
            HIKARI_LOG_INFO("[MaterialImporter] validate only: " + record.sourcePath.generic_string());
            return result;
        }

        std::string writeMessage{};
        if (!WriteHmatFile(outputPath, data, compileResult, writeMessage)) {
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
        result.message = compileResult.success
            ? "[MaterialImporter] Material v4 cooked HMAT: " + relativeOutput.generic_string()
            : "[MaterialImporter][WARN] Material v4 cooked HMAT with last-good/fallback: " +
                relativeOutput.generic_string();
        HIKARI_LOG_INFO("[MaterialImporter] cooked HMAT: " + relativeOutput.generic_string());
        return result;
    }

} // namespace HIKARI
