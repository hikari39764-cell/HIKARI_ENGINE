#include "Assets/Importers/HIKARI_DialogueAssetImporter.h"

#include <json.hpp>

#include "Assets/Dialogue/HIKARI_DialogueAsset.h"

namespace HIKARI {

    ASSETS::SEMANTICS::AssetImporterKind
        DialogueAssetImporter::GetImporterKind() const noexcept {
        return ASSETS::SEMANTICS::AssetImporterKind::Dialogue;
    }

    AssetMeta DialogueAssetImporter::CreateDefaultMeta(
        const std::filesystem::path& sourcePath,
        const AssetGuid& guid) const {
        AssetMeta meta = ASSETS::SEMANTICS::MakeBaseAssetMeta(
            sourcePath,
            guid,
            GetImporterKind());
        meta.importSettingsJson = nlohmann::json{
            { "runtimeLoader", "DialogueAssetStore" },
            { "sourceAuthoritative", true },
        }.dump(2);
        return meta;
    }

    AssetImportResult DialogueAssetImporter::Import(
        const AssetRecord& record,
        const AssetImportContext& context) {
        AssetImportResult result{};
        DialogueAsset asset{};
        std::string error{};
        const std::filesystem::path sourcePath = record.sourcePath.is_absolute()
            ? record.sourcePath
            : (context.projectRoot / record.sourcePath).lexically_normal();
        result.success = LoadDialogueAsset(
            sourcePath,
            record.guid,
            asset,
            &error);
        const auto issues = result.success
            ? DIALOGUE::ValidateDialogue(asset.definition)
            : std::vector<DIALOGUE::DialogueValidationIssue>{};
        size_t errorCount = 0u;
        for (const auto& issue : issues) {
            if (issue.error) ++errorCount;
        }
        result.success = result.success && errorCount == 0u;
        result.message = result.success
            ? "Dialogue asset validated"
            : "Dialogue asset validation failed: " + error;
        result.diagnosticsJson = nlohmann::json{
            { "kind", "Dialogue" },
            { "nodeCount", asset.definition.nodes.size() },
            { "validationIssueCount", issues.size() },
            { "validationErrorCount", errorCount },
            { "valid", result.success },
        }.dump(2);
        return result;
    }

} // namespace HIKARI
