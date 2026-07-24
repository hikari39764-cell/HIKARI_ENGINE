#include "HIKARI_TextureImporter.h"

#include <exception>
#include <filesystem>

#include <json.hpp>

#include "Assets/Importers/Policy/HIKARI_TextureImportPolicy.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Core/IO/HIKARI_FileReplacementTransaction.h"
#include "Core/HIKARI_Logger.h"
#include "Project/Paths/HIKARI_ProjectPath.h"
#include "Assets/Tasks/HIKARI_AssetTaskService.h"
#include "HIKARI_HtexTextureWriter_DirectXTex.h"

namespace HIKARI {

    TextureImporter::TextureImporter(std::unique_ptr<ITextureImportBackend> backend)
        : backend_(std::move(backend)) {
    }

    ASSETS::SEMANTICS::AssetImporterKind
        TextureImporter::GetImporterKind() const noexcept {
        return ASSETS::SEMANTICS::AssetImporterKind::Texture;
    }

    AssetMeta TextureImporter::CreateDefaultMeta(
        const std::filesystem::path& sourcePath,
        const AssetGuid& guid) const {

        const TextureImportSettings settings =
            ASSETS::IMPORT_POLICY::ResolveTextureImportSettings(
                sourcePath,
                {});

        AssetMeta meta = ASSETS::SEMANTICS::MakeBaseAssetMeta(
            sourcePath,
            guid,
            GetImporterKind());
        meta.importSettingsJson =
            ASSETS::IMPORT_POLICY::MakeTextureImportSettingsJson(settings);
        return meta;
    }

    AssetImportResult TextureImporter::Import(
        const AssetRecord& record,
        const AssetImportContext& context) {

        AssetImportResult result{};
        const std::string taskItem =
            record.sourcePath.filename().string();
        if (!backend_ || !backend_->IsAvailable()) {
            result.message = "[TextureImporter] Backend Missing";
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        TextureImportSettings settings =
            ASSETS::IMPORT_POLICY::ResolveTextureImportSettings(
                record.meta.sourcePath,
                record.meta.importSettingsJson);
        std::string inspectMessage{};
        if (context.task != nullptr) {
            context.task->ReportStage(
                "Inspecting texture",
                0.05f,
                true,
                taskItem);
            if (context.task->IsCancellationRequested()) {
                result.message = "[TextureImporter] import canceled";
                return result;
            }
        }
        try {
            if (!backend_->Inspect(context.projectRoot / record.sourcePath, settings, inspectMessage)) {
                result.message = inspectMessage.empty() ? "[TextureImporter] inspect failed" : inspectMessage;
                return result;
            }
        } catch (const std::exception& ex) {
            result.message = std::string("[TextureImporter] inspect exception: ") + ex.what();
            HIKARI_LOG_ERROR(result.message);
            return result;
        } catch (...) {
            result.message = "[TextureImporter] inspect exception: unknown";
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        const std::filesystem::path finalPath = context.importedDirectory / "texture.dds";
        const std::filesystem::path tempPath = context.importedDirectory / "texture.importing.dds";
        const std::filesystem::path finalHtexPath = context.importedDirectory / "texture.htex";
        const std::filesystem::path tempHtexPath = context.importedDirectory / "texture.importing.htex";

        if (context.task != nullptr) {
            context.task->ReportStage(
                "Preparing texture artifacts",
                0.16f,
                true,
                taskItem);
        }
        std::error_code removeEc{};
        std::filesystem::remove(tempPath, removeEc);
        if (removeEc) {
            result.message = "[TextureImporter] failed to clear stale temporary DDS: " + tempPath.generic_string();
            HIKARI_LOG_ERROR(result.message);
            return result;
        }
        removeEc.clear();
        std::filesystem::remove(tempHtexPath, removeEc);
        if (removeEc) {
            result.message = "[TextureImporter] failed to clear stale temporary HTEX: " + tempHtexPath.generic_string();
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        std::string convertMessage{};
        try {
            if (!backend_->ConvertToHtexAndDds(
                    context.projectRoot / record.sourcePath,
                    tempHtexPath,
                    tempPath,
                    settings,
                    convertMessage,
                    context.task)) {
                std::error_code cleanupEc{};
                std::filesystem::remove(tempHtexPath, cleanupEc);
                std::filesystem::remove(tempPath, cleanupEc);
                result.message = convertMessage.empty() ? "[TextureImporter] import failed" : convertMessage;
                return result;
            }
        } catch (const std::exception& ex) {
            std::error_code cleanupEc{};
            std::filesystem::remove(tempHtexPath, cleanupEc);
            std::filesystem::remove(tempPath, cleanupEc);
            result.message = std::string("[TextureImporter] import exception: ") + ex.what();
            HIKARI_LOG_ERROR(result.message);
            return result;
        } catch (...) {
            std::error_code cleanupEc{};
            std::filesystem::remove(tempHtexPath, cleanupEc);
            std::filesystem::remove(tempPath, cleanupEc);
            result.message = "[TextureImporter] import exception: unknown";
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        if (context.task != nullptr &&
            context.task->IsCancellationRequested()) {
            std::error_code cleanupEc{};
            std::filesystem::remove(tempHtexPath, cleanupEc);
            std::filesystem::remove(tempPath, cleanupEc);
            result.message = "[TextureImporter] import canceled";
            return result;
        }

        if (context.task != nullptr) {
            context.task->ReportStage(
                "Committing texture artifacts",
                0.97f,
                true,
                taskItem);
        }
        const IO::FileReplacementOperation replacements[]{
            {
                .finalPath = finalHtexPath,
                .stagedPath = tempHtexPath,
            },
            {
                .finalPath = finalPath,
                .stagedPath = tempPath,
            },
        };
        std::string commitMessage{};
        if (!IO::CommitFileReplacementTransaction(
            replacements,
            commitMessage)) {
            std::error_code cleanupEc{};
            std::filesystem::remove(tempHtexPath, cleanupEc);
            std::filesystem::remove(tempPath, cleanupEc);
            result.message =
                "[TextureImporter] failed to commit texture artifacts: " +
                commitMessage;
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        result.success = true;
        result.message = "[TextureImporter] Imported with DirectXTex and wrote HTEX";
        result.diagnosticsJson = nlohmann::json{
            { "format", "HTEX" },
            { "texture", {
                { "usage", std::string(ASSETS::IMPORT_POLICY::ToString(settings.usage)) },
                { "dimension", std::string(ASSETS::IMPORT_POLICY::ToString(settings.dimension)) },
                { "colorSpace", std::string(ASSETS::IMPORT_POLICY::ToString(settings.colorSpace)) },
                { "compression", std::string(ASSETS::IMPORT_POLICY::ToString(settings.compression)) },
                { "sourceHasAlphaChannel", settings.sourceHasAlphaChannel },
                { "sourceHasMeaningfulAlpha", settings.sourceHasMeaningfulAlpha },
                { "sourceHasTranslucentAlpha", settings.sourceHasTranslucentAlpha },
                { "sourceHasCutoutAlpha", settings.sourceHasCutoutAlpha },
                { "sourceAlphaNonOpaqueRatio", settings.sourceAlphaNonOpaqueRatio },
                { "sourceAlphaTranslucentRatio", settings.sourceAlphaTranslucentRatio },
                { "sourceAlphaCutoutRatio", settings.sourceAlphaCutoutRatio },
            } },
        }.dump(2);
        result.artifacts.push_back(
            ASSETS::SEMANTICS::MakeAssetArtifact(
                ASSETS::SEMANTICS::AssetArtifactKind::MainTexture,
                PROJECT_PATHS::MakeProjectRelativeString(
                    context.projectRoot,
                    finalHtexPath)));
        result.artifacts.push_back(
            ASSETS::SEMANTICS::MakeAssetArtifact(
                ASSETS::SEMANTICS::AssetArtifactKind::DebugTextureDds,
                PROJECT_PATHS::MakeProjectRelativeString(
                    context.projectRoot,
                    finalPath)));

        HIKARI_LOG_INFO(result.message + " source=" + record.sourcePath.generic_string() + " guid=" + record.guid.value);
        return result;
    }

} // namespace HIKARI
