#include "Assets/Importers/HIKARI_CombatActionAssetImporter.h"

#include <json.hpp>

#include "Assets/Action/HIKARI_CombatActionAsset.h"

namespace HIKARI {

    ASSETS::SEMANTICS::AssetImporterKind
        CombatActionAssetImporter::GetImporterKind() const noexcept {
        return ASSETS::SEMANTICS::AssetImporterKind::CombatAction;
    }

    AssetMeta CombatActionAssetImporter::CreateDefaultMeta(
        const std::filesystem::path& sourcePath,
        const AssetGuid& guid) const {
        AssetMeta meta = ASSETS::SEMANTICS::MakeBaseAssetMeta(
            sourcePath, guid, GetImporterKind());
        meta.importSettingsJson = nlohmann::json{
            { "sourceAuthoritative", true },
            { "runtimeEvaluator", "CombatActionEvaluator" }
        }.dump(2);
        return meta;
    }

    AssetImportResult CombatActionAssetImporter::Import(
        const AssetRecord& record,
        const AssetImportContext& context) {
        AssetImportResult result{};
        ACTION::CombatActionAsset asset{};
        std::string error{};
        const std::filesystem::path sourcePath = record.sourcePath.is_absolute()
            ? record.sourcePath
            : (context.projectRoot / record.sourcePath).lexically_normal();
        result.success = ACTION::LoadCombatActionAsset(
            sourcePath, record.guid, asset, &error);
        const auto diagnostics = result.success
            ? ACTION::ValidateCombatAction(asset.definition)
            : std::vector<ACTION::CombatActionDiagnostic>{};
        size_t warningCount = 0u;
        for (const auto& diagnostic : diagnostics) {
            if (diagnostic.severity ==
                    ACTION::CombatActionDiagnosticSeverity::Warning) {
                ++warningCount;
            }
        }
        result.message = result.success
            ? "Combat action asset validated"
            : "Combat action asset validation failed: " + error;
        result.diagnosticsJson = nlohmann::json{
            { "kind", "CombatAction" },
            { "actionId", asset.definition.actionId },
            { "durationSeconds", asset.definition.durationSeconds },
            { "animationSegmentCount", asset.definition.animationSegments.size() },
            { "hitWindowCount", asset.definition.hitWindows.size() },
            { "warningCount", warningCount },
            { "valid", result.success }
        }.dump(2);
        return result;
    }

} // namespace HIKARI
