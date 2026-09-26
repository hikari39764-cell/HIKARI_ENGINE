#include "Assets/Importers/HIKARI_ModelMaterialBindingImporter.h"

#include <json.hpp>

#include "Assets/Models/MaterialBindings/HIKARI_ModelMaterialBindingAsset.h"

namespace HIKARI {

ASSETS::SEMANTICS::AssetImporterKind ModelMaterialBindingImporter::GetImporterKind() const noexcept {
    return ASSETS::SEMANTICS::AssetImporterKind::ModelMaterialBinding;
}

AssetMeta ModelMaterialBindingImporter::CreateDefaultMeta(const std::filesystem::path& sourcePath,
                                                          const AssetGuid& guid) const {
    AssetMeta meta = ASSETS::SEMANTICS::MakeBaseAssetMeta(sourcePath, guid, GetImporterKind());
    meta.importSettingsJson =
        nlohmann::json{{"runtimeLoader", "ModelMaterialBindingAsset"}, {"sourceAuthoritative", true}}.dump(2);
    return meta;
}

AssetImportResult ModelMaterialBindingImporter::Import(const AssetRecord& record, const AssetImportContext& context) {
    AssetImportResult result{};
    if (!ASSETS::SEMANTICS::IsAssetSourceForImporter(record.sourcePath, GetImporterKind())) {
        result.message = "Unsupported ModelMaterialBinding source: " + record.sourcePath.generic_string();
        return result;
    }
    const std::filesystem::path sourcePath = record.sourcePath.is_absolute()
                                                 ? record.sourcePath
                                                 : (context.projectRoot / record.sourcePath).lexically_normal();
    ASSETS::MODELS::ModelMaterialBindingAssetData data{};
    std::vector<std::string> diagnostics{};
    std::string error{};
    result.success = ASSETS::MODELS::LoadModelMaterialBindingAsset(sourcePath, data, error, &diagnostics);
    if (!result.success) {
        result.message = "ModelMaterialBinding validation failed: " + error;
        result.diagnosticsJson =
            nlohmann::json{{"kind", "ModelMaterialBinding"}, {"valid", false}, {"diagnostics", diagnostics}}.dump(2);
        return result;
    }
    for (const auto& dependency : ASSETS::MODELS::CollectModelMaterialBindingDependencies(data)) {
        result.dependencies.push_back(AssetDependencyDesc{dependency.assetGuid, {}, dependency.role});
    }
    result.message = "ModelMaterialBinding asset validated";
    result.diagnosticsJson = nlohmann::json{
        {"kind", "ModelMaterialBinding"},
        {"valid", true},
        {"modelAssetGuid", data.modelAssetGuid.value},
        {"bindingCount", data.bindings.size()},
        {"diagnostics",
         diagnostics}}.dump(2);
    return result;
}

} // namespace HIKARI
