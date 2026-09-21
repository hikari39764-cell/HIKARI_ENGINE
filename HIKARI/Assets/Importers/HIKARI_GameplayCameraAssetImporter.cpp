#include "Assets/Importers/HIKARI_GameplayCameraAssetImporter.h"
#include "Assets/Camera/HIKARI_GameplayCameraAsset.h"
namespace HIKARI {
ASSETS::SEMANTICS::AssetImporterKind GameplayCameraAssetImporter::GetImporterKind() const noexcept {
    return ASSETS::SEMANTICS::AssetImporterKind::GameplayCamera;
}
AssetMeta GameplayCameraAssetImporter::CreateDefaultMeta(const std::filesystem::path &sourcePath,
                                                         const AssetGuid &guid) const {
    return ASSETS::SEMANTICS::MakeBaseAssetMeta(sourcePath, guid, GetImporterKind());
}
AssetImportResult GameplayCameraAssetImporter::Import(const AssetRecord &record, const AssetImportContext &context) {
    AssetImportResult result;
    CAMERA::GameplayCameraProfile profile;
    const auto path = record.sourcePath.is_absolute() ? record.sourcePath : context.projectRoot / record.sourcePath;
    result.success = LoadGameplayCameraAsset(path, profile, result.message);
    if (result.success)
        result.message = "Gameplay camera profile validated";
    result.diagnosticsJson = nlohmann::json{{"valid", result.success}, {"modeCount", profile.modes.size()}}.dump();
    return result;
}
} // namespace HIKARI
