#include "Assets/Material/Authoring/HIKARI_MaterialAssetAuthoringService.h"

#include <algorithm>
#include <cctype>
#include <system_error>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Assets/Material/HIKARI_MaterialAssetLookup.h"
#include "Core/Text/HIKARI_AsciiCase.h"

namespace HIKARI::ASSETS::MATERIAL {

    namespace {

        constexpr std::string_view kMaterialSourceSuffix =
            ".material.json";

        std::string SanitizeFileStem(std::string value) {
            for (char& character : value) {
                const unsigned char code =
                    static_cast<unsigned char>(character);
                if (!std::isalnum(code) && character != '-' &&
                    character != '_') {
                    character = '_';
                }
            }
            value.erase(std::unique(value.begin(), value.end(),
                [](char left, char right) {
                    return left == '_' && right == '_';
                }), value.end());
            return value.empty() ? "Material" : value;
        }

        bool EndsWithMaterialSuffix(
            const std::filesystem::path& path) {

            const std::string filename =
                TEXT::ToLowerAsciiCopy(path.filename().string());
            return filename.ends_with(kMaterialSourceSuffix);
        }

        bool IsUnderRoot(
            const std::filesystem::path& root,
            const std::filesystem::path& path) {

            std::error_code ec{};
            const std::filesystem::path relative =
                std::filesystem::relative(path, root, ec);
            if (ec || relative.empty() || relative.is_absolute()) {
                return false;
            }
            return *relative.begin() != "..";
        }

        std::filesystem::path ResolveSourcePath(
            const AssetDatabase& database,
            const AssetRecord& record) {

            return record.sourcePath.is_absolute()
                ? record.sourcePath.lexically_normal()
                : (database.GetProjectRoot() / record.sourcePath)
                    .lexically_normal();
        }

        std::filesystem::path ResolveDestination(
            const AssetDatabase& database,
            std::filesystem::path destination) {

            if (destination.empty()) {
                return {};
            }
            if (!destination.is_absolute()) {
                destination = database.GetAssetsRoot() / destination;
            }
            if (!EndsWithMaterialSuffix(destination)) {
                destination += kMaterialSourceSuffix;
            }
            return destination.lexically_normal();
        }

    } // namespace

    MaterialAssetAuthoringService::MaterialAssetAuthoringService(
        AssetDatabase& assetDatabase) noexcept
        : assetDatabase_(assetDatabase) {
    }

    bool MaterialAssetAuthoringService::Load(
        const AssetGuid& assetGuid,
        MaterialAuthoringDocument& outDocument,
        std::string& outMessage) const {

        const AssetRecord* record = assetDatabase_.FindByGuid(assetGuid);
        if (record == nullptr || record->type != AssetType::Material) {
            outMessage = "Selected asset is not a Material asset";
            return false;
        }
        MaterialAuthoringDocument document{};
        document.assetGuid = record->guid;
        document.sourcePath = record->sourcePath;
        if (!LoadMaterialAssetData(
                ResolveSourcePath(assetDatabase_, *record),
                document.source, outMessage,
                &document.migrationDiagnostics)) {
            return false;
        }
        document.compile = CompileMaterialAsset(
            document.source,
            MakeMaterialCompileOptions(assetDatabase_));
        outDocument = std::move(document);
        outMessage = "Material asset loaded";
        return true;
    }

    MaterialInstanceResolveResult
        MaterialAssetAuthoringService::ResolveInstance(
            const MaterialAssetData& instanceSource) const {

        return ResolveMaterialInstance(
            instanceSource,
            [this](
                const AssetGuid& guid,
                MaterialAssetData& outSource,
                std::string& outError) {
                return LoadMaterialAssetSource(
                    assetDatabase_, guid, outSource, outError);
            },
            16u,
            MakeMaterialCompileOptions(assetDatabase_));
    }

    MaterialAuthoringResult MaterialAssetAuthoringService::Create(
        MaterialTemplateId templateId,
        std::string_view displayName,
        const std::filesystem::path& directory) {

        return Create(
            CreateMaterialFromTemplate(templateId, displayName),
            directory);
    }

    MaterialAuthoringResult MaterialAssetAuthoringService::Create(
        const MaterialAssetData& source,
        const std::filesystem::path& directory) {

        const std::filesystem::path root = directory.empty()
            ? assetDatabase_.GetAssetsRoot() / "Materials"
            : (directory.is_absolute()
                ? directory
                : assetDatabase_.GetAssetsRoot() / directory);
        const std::string stem = SanitizeFileStem(source.materialName);
        std::filesystem::path path =
            root / (stem + std::string(kMaterialSourceSuffix));
        for (uint32_t suffix = 2u;
            std::filesystem::exists(path); ++suffix) {
            path = root / (stem + "_" + std::to_string(suffix) +
                std::string(kMaterialSourceSuffix));
        }
        return SaveToPath(source, path, true);
    }

    MaterialAuthoringResult MaterialAssetAuthoringService::Update(
        const AssetGuid& assetGuid,
        const MaterialAssetData& source) {

        const AssetRecord* record = assetDatabase_.FindByGuid(assetGuid);
        if (record == nullptr || record->type != AssetType::Material) {
            MaterialAuthoringResult result{};
            result.message = "Selected asset is not a Material asset";
            return result;
        }
        return SaveToPath(
            source,
            ResolveSourcePath(assetDatabase_, *record),
            false);
    }

    MaterialAuthoringResult MaterialAssetAuthoringService::Save(
        const AssetGuid& assetGuid,
        const MaterialAssetData& source) {

        return Update(assetGuid, source);
    }

    MaterialAuthoringResult MaterialAssetAuthoringService::SaveAs(
        const MaterialAssetData& source,
        const std::filesystem::path& destination) {

        return SaveToPath(
            source,
            ResolveDestination(assetDatabase_, destination),
            true);
    }

    MaterialAuthoringResult
        MaterialAssetAuthoringService::ImportAndScan(
            const std::filesystem::path& sourcePath,
            bool createMissingMeta) {

        MaterialAuthoringResult result{};
        const std::filesystem::path resolved = sourcePath.is_absolute()
            ? sourcePath.lexically_normal()
            : (assetDatabase_.GetAssetsRoot() / sourcePath)
                .lexically_normal();
        result.sourcePath = resolved;
        if (!IsUnderRoot(assetDatabase_.GetAssetsRoot(), resolved) ||
            !EndsWithMaterialSuffix(resolved)) {
            result.message =
                "Material source must be a .material.json file under Assets";
            return result;
        }
        result.assetScanned =
            assetDatabase_.ScanAssets(createMissingMeta);
        if (!result.assetScanned) {
            result.message = "Material source was saved, but asset scan failed";
            return result;
        }
        const AssetRecord* record = assetDatabase_.FindByPath(resolved);
        if (record == nullptr || record->type != AssetType::Material ||
            !record->guid.IsValid()) {
            result.message =
                "Material source was saved, but no Material record was created";
            return result;
        }
        result.assetGuid = record->guid;
        result.sourcePath = record->sourcePath;
        result.imported = assetDatabase_.ImportAsset(record->guid);
        result.success = result.imported;
        result.message = result.success
            ? "Material asset scanned and imported"
            : "Material asset was scanned, but import failed";
        return result;
    }

    MaterialAuthoringResult MaterialAssetAuthoringService::SaveToPath(
        const MaterialAssetData& source,
        const std::filesystem::path& destination,
        bool createMissingMeta) {

        MaterialAuthoringResult result{};
        result.sourcePath = destination;
        // The serialized graph and its compiled runtime meaning are one unit.
        result.compile = CompileMaterialAsset(
            source,
            MakeMaterialCompileOptions(assetDatabase_));
        if (!result.compile.success) {
            result.message =
                "Material v6 graph is invalid; source was not saved";
            return result;
        }
        if (destination.empty() ||
            !IsUnderRoot(assetDatabase_.GetAssetsRoot(), destination) ||
            !EndsWithMaterialSuffix(destination)) {
            result.message =
                "Material destination must be a .material.json file under Assets";
            return result;
        }
        std::string error{};
        result.sourceSaved = SaveMaterialAssetData(
            destination, source, error);
        if (!result.sourceSaved) {
            result.message = std::move(error);
            return result;
        }
        MaterialAuthoringResult importResult =
            ImportAndScan(destination, createMissingMeta);
        importResult.sourceSaved = true;
        importResult.compile = result.compile;
        return importResult;
    }

} // namespace HIKARI::ASSETS::MATERIAL
