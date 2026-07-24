#include "HIKARI_SkyCubemapImporter.h"

#include <filesystem>

#include <json.hpp>

#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Core/IO/HIKARI_FileReplacementTransaction.h"
#include "Core/HIKARI_Logger.h"
#include "Project/Paths/HIKARI_ProjectPath.h"
#include "HIKARI_IblBaker.h"

namespace HIKARI {

    namespace {

        nlohmann::json MakeDefaultSettings() {
            return nlohmann::json{
                { "sourceDimension", "TextureCube" },
                { "colorSpace", "Linear" },
                { "copySkyCubemap", true },
                { "autoBakeIBL", true },
                { "irradianceSize", 64 },
                { "irradianceSampleCount", 256 },
                { "prefilteredSize", 256 },
                { "prefilteredMipCount", 7 },
                { "prefilteredSampleCount", 1024 },
                { "brdfLutSize", 256 },
                { "brdfSampleCount", 1024 },
                { "outputFormat", "DDS" },
            };
        }

        nlohmann::json ReadSettings(const AssetMeta& meta) {
            nlohmann::json settings = nlohmann::json::parse(meta.importSettingsJson, nullptr, false);
            if (!settings.is_object()) {
                settings = MakeDefaultSettings();
            }
            return settings;
        }

        bool CopyFileToTemp(
            const std::filesystem::path& sourcePath,
            const std::filesystem::path& tempPath,
            std::string& outMessage) {

            std::error_code ec{};
            std::filesystem::create_directories(tempPath.parent_path(), ec);
            if (ec) {
                outMessage = "[SkyCubemapImporter] failed to create output directory: " + tempPath.parent_path().generic_string();
                return false;
            }

            std::filesystem::copy_file(
                sourcePath,
                tempPath,
                std::filesystem::copy_options::overwrite_existing,
                ec);
            if (ec) {
                outMessage = "[SkyCubemapImporter] failed to copy cubemap: " + ec.message();
                return false;
            }
            return true;
        }
    }

    SkyCubemapImporter::SkyCubemapImporter(std::unique_ptr<ITextureImportBackend> backend)
        : backend_(std::move(backend)) {
    }

    ASSETS::SEMANTICS::AssetImporterKind
        SkyCubemapImporter::GetImporterKind() const noexcept {
        return ASSETS::SEMANTICS::AssetImporterKind::SkyCubemap;
    }

    AssetMeta SkyCubemapImporter::CreateDefaultMeta(
        const std::filesystem::path& sourcePath,
        const AssetGuid& guid) const {

        AssetMeta meta = ASSETS::SEMANTICS::MakeBaseAssetMeta(
            sourcePath,
            guid,
            GetImporterKind());
        meta.importSettingsJson = MakeDefaultSettings().dump(2);
        return meta;
    }

    AssetImportResult SkyCubemapImporter::Import(
        const AssetRecord& record,
        const AssetImportContext& context) {

        AssetImportResult result{};
        if (!backend_ || !backend_->IsAvailable()) {
            result.message = "[SkyCubemapImporter] Backend Missing";
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        nlohmann::json settingsJson = ReadSettings(record.meta);
        TextureImportSettings textureSettings{};
        textureSettings.usage = TextureUsage::SkyCubemap;
        textureSettings.dimension = TextureAssetDimension::TextureCube;
        textureSettings.colorSpace = TextureAssetColorSpace::Srgb;
        textureSettings.compression = TextureCompression::None;
        textureSettings.mipPolicy = TextureMipPolicy::Preserve;

        std::string inspectMessage{};
        if (!backend_->Inspect(context.projectRoot / record.sourcePath, textureSettings, inspectMessage)) {
            result.message = inspectMessage.empty() ? "[SkyCubemapImporter] inspect failed" : inspectMessage;
            return result;
        }

        if (textureSettings.dimension != TextureAssetDimension::TextureCube) {
            result.message = "[SkyCubemapImporter] source DDS is not a TextureCube";
            HIKARI_LOG_ERROR(result.message + " source=" + record.sourcePath.generic_string());
            return result;
        }

        const std::filesystem::path finalPath = context.importedDirectory / "sky.dds";
        const std::filesystem::path tempPath = context.importedDirectory / "sky.dds.tmp";
        const bool copySkyCubemap = settingsJson.value("copySkyCubemap", true);

        if (copySkyCubemap) {
            if (!CopyFileToTemp(context.projectRoot / record.sourcePath, tempPath, result.message)) {
                HIKARI_LOG_ERROR(result.message);
                return result;
            }
        } else {
            std::string convertMessage{};
            if (!backend_->ConvertToDds(context.projectRoot / record.sourcePath, tempPath, textureSettings, convertMessage)) {
                result.message = convertMessage.empty() ? "[SkyCubemapImporter] normalize failed" : convertMessage;
                return result;
            }
        }

        std::string commitMessage{};
        if (!IO::CommitStagedFile(
            tempPath,
            finalPath,
            commitMessage)) {
            std::error_code cleanupEc{};
            std::filesystem::remove(tempPath, cleanupEc);
            result.message =
                "[SkyCubemapImporter] failed to replace output DDS: " +
                commitMessage;
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        result.success = true;
        result.message = "[SkyCubemapImporter] Imported sky cubemap";
        result.artifacts.push_back(
            ASSETS::SEMANTICS::MakeAssetArtifact(
                ASSETS::SEMANTICS::AssetArtifactKind::SkyCubemap,
                PROJECT_PATHS::MakeProjectRelativeString(
                    context.projectRoot,
                    finalPath)));

        if (settingsJson.value("autoBakeIBL", true)) {
            IblBakeSettings iblSettings{};
            iblSettings.irradianceSize = settingsJson.value("irradianceSize", 64u);
            iblSettings.prefilteredSize = settingsJson.value("prefilteredSize", 256u);
            iblSettings.prefilteredMipCount = settingsJson.value("prefilteredMipCount", 7u);
            iblSettings.irradianceSampleCount = settingsJson.value("irradianceSampleCount", 256u);
            iblSettings.prefilteredSampleCount = settingsJson.value("prefilteredSampleCount", 1024u);
            iblSettings.brdfLutSize = settingsJson.value("brdfLutSize", 256u);
            iblSettings.brdfSampleCount = settingsJson.value("brdfSampleCount", 1024u);

            const IblBakeResult bake = IblBaker::BakeSkyCubemapToIbl(
                finalPath,
                context.importedDirectory / "IBL",
                context.projectRoot / "Library" / "Generated" / "IBL",
                iblSettings);

            if (bake.success) {
                result.artifacts.push_back(
                    ASSETS::SEMANTICS::MakeAssetArtifact(
                        ASSETS::SEMANTICS::AssetArtifactKind::IblIrradiance,
                        PROJECT_PATHS::MakeProjectRelativeString(
                            context.projectRoot,
                            bake.irradiancePath)));
                result.artifacts.push_back(
                    ASSETS::SEMANTICS::MakeAssetArtifact(
                        ASSETS::SEMANTICS::AssetArtifactKind::IblPrefiltered,
                        PROJECT_PATHS::MakeProjectRelativeString(
                            context.projectRoot,
                            bake.prefilteredPath)));
                result.artifacts.push_back(
                    ASSETS::SEMANTICS::MakeAssetArtifact(
                        ASSETS::SEMANTICS::AssetArtifactKind::BrdfLut,
                        PROJECT_PATHS::MakeProjectRelativeString(
                            context.projectRoot,
                            bake.brdfLutPath)));
                result.message += "; IBL baked";
            } else {
                result.message += "; IBL bake failed: " + bake.message;
                HIKARI_LOG_WARN("[SkyCubemapImporter] IBL bake failed: " + bake.message);
            }
        }

        HIKARI_LOG_INFO(result.message + " source=" + record.sourcePath.generic_string() + " guid=" + record.guid.value);
        return result;
    }

} // namespace HIKARI
