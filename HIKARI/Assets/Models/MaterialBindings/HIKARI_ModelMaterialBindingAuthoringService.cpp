#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Assets/Models/MaterialBindings/HIKARI_ModelMaterialBindingAuthoringService.h"

#include <algorithm>
#include <cctype>
#include <system_error>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Core/Text/HIKARI_AsciiCase.h"

namespace HIKARI::ASSETS::MODELS {

namespace {

constexpr std::string_view kBindingSourceSuffix = ".hmodelmaterial";

bool HasBindingSuffix(const std::filesystem::path& path) {
    return TEXT::ToLowerAsciiCopy(path.extension().string()) == kBindingSourceSuffix;
}

bool IsUnderRoot(const std::filesystem::path& root, const std::filesystem::path& path) {
    std::error_code ec{};
    const std::filesystem::path relative = std::filesystem::relative(path, root, ec);
    return !ec && !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}

std::filesystem::path ResolveSourcePath(const AssetDatabase& database, const AssetRecord& record) {
    return record.sourcePath.is_absolute() ? record.sourcePath.lexically_normal()
                                           : (database.GetProjectRoot() / record.sourcePath).lexically_normal();
}

std::filesystem::path ResolveDestination(const AssetDatabase& database, std::filesystem::path destination) {
    if (destination.empty()) {
        return {};
    }
    if (!destination.is_absolute()) {
        destination = database.GetAssetsRoot() / destination;
    }
    if (!HasBindingSuffix(destination)) {
        destination += kBindingSourceSuffix;
    }
    return destination.lexically_normal();
}

} // namespace

ModelMaterialBindingAuthoringService::ModelMaterialBindingAuthoringService(AssetDatabase& assetDatabase) noexcept
    : assetDatabase_(assetDatabase) {}

bool ModelMaterialBindingAuthoringService::Load(const AssetGuid& assetGuid,
                                                ModelMaterialBindingAuthoringDocument& outDocument,
                                                std::string& outMessage) const {
    const AssetRecord* record = assetDatabase_.FindByGuid(assetGuid);
    if (record == nullptr || record->type != AssetType::ModelMaterialBinding) {
        outMessage = "Selected asset is not a ModelMaterialBinding asset";
        return false;
    }
    ModelMaterialBindingAuthoringDocument document{};
    document.assetGuid = record->guid;
    document.sourcePath = record->sourcePath;
    if (!LoadModelMaterialBindingAsset(ResolveSourcePath(assetDatabase_, *record), document.data, outMessage,
                                       &document.migrationDiagnostics)) {
        return false;
    }
    outDocument = std::move(document);
    outMessage = "Model material binding asset loaded";
    return true;
}

ModelMaterialBindingAuthoringResult ModelMaterialBindingAuthoringService::Create(
    const AssetGuid& modelAssetGuid, std::string_view displayName, const std::filesystem::path& directory) {
    ModelMaterialBindingAssetData data{};
    data.displayName = displayName.empty() ? "Model Material Bindings" : std::string(displayName);
    data.modelAssetGuid = modelAssetGuid;
    const std::filesystem::path root =
        directory.empty() ? assetDatabase_.GetAssetsRoot() / "Models" / "MaterialBindings"
                          : (directory.is_absolute() ? directory : assetDatabase_.GetAssetsRoot() / directory);
    const std::string stem = ASSETS::SEMANTICS::MakeAssetFileStem(data.displayName, "ModelMaterialBindings");
    std::filesystem::path path = root / (stem + std::string(kBindingSourceSuffix));
    for (uint32_t suffix = 2u; std::filesystem::exists(path); ++suffix) {
        path = root / (stem + "_" + std::to_string(suffix) + std::string(kBindingSourceSuffix));
    }
    return SaveToPath(data, path, true);
}

ModelMaterialBindingAuthoringResult ModelMaterialBindingAuthoringService::Update(
    const AssetGuid& assetGuid, const ModelMaterialBindingAssetData& data) {
    const AssetRecord* record = assetDatabase_.FindByGuid(assetGuid);
    if (record == nullptr || record->type != AssetType::ModelMaterialBinding) {
        ModelMaterialBindingAuthoringResult result{};
        result.message = "Selected asset is not a ModelMaterialBinding asset";
        return result;
    }
    return SaveToPath(data, ResolveSourcePath(assetDatabase_, *record), false);
}

ModelMaterialBindingAuthoringResult ModelMaterialBindingAuthoringService::Save(
    const AssetGuid& assetGuid, const ModelMaterialBindingAssetData& data) {
    return Update(assetGuid, data);
}

ModelMaterialBindingAuthoringResult ModelMaterialBindingAuthoringService::SaveAs(
    const ModelMaterialBindingAssetData& data, const std::filesystem::path& destination) {
    return SaveToPath(data, ResolveDestination(assetDatabase_, destination), true);
}

ModelMaterialBindingAuthoringResult ModelMaterialBindingAuthoringService::ImportAndScan(
    const std::filesystem::path& sourcePath, bool createMissingMeta) {
    ModelMaterialBindingAuthoringResult result{};
    const std::filesystem::path resolved = sourcePath.is_absolute()
                                               ? sourcePath.lexically_normal()
                                               : (assetDatabase_.GetAssetsRoot() / sourcePath).lexically_normal();
    result.sourcePath = resolved;
    if (!IsUnderRoot(assetDatabase_.GetAssetsRoot(), resolved) || !HasBindingSuffix(resolved)) {
        result.message = "Binding source must be a .hmodelmaterial file under Assets";
        return result;
    }
    result.assetScanned = assetDatabase_.ScanAssets(createMissingMeta);
    if (!result.assetScanned) {
        result.message = "Binding source was saved, but asset scan failed";
        return result;
    }
    const AssetRecord* record = assetDatabase_.FindByPath(resolved);
    if (record == nullptr || record->type != AssetType::ModelMaterialBinding || !record->guid.IsValid()) {
        result.message = "Binding source was saved, but no binding record was created";
        return result;
    }
    result.assetGuid = record->guid;
    result.sourcePath = record->sourcePath;
    result.imported = assetDatabase_.ImportAsset(record->guid);
    result.success = result.imported;
    result.message = result.success ? "Model material binding scanned and imported"
                                    : "Model material binding was scanned, but import failed";
    return result;
}

ModelMaterialBindingAuthoringResult ModelMaterialBindingAuthoringService::SaveToPath(
    const ModelMaterialBindingAssetData& data, const std::filesystem::path& destination, bool createMissingMeta) {
    ModelMaterialBindingAuthoringResult result{};
    result.sourcePath = destination;
    if (!ValidateModelMaterialBindingAsset(data, result.validationDiagnostics)) {
        result.message = result.validationDiagnostics.empty() ? "Model material binding validation failed"
                                                              : result.validationDiagnostics.front();
        return result;
    }
    if (destination.empty() || !IsUnderRoot(assetDatabase_.GetAssetsRoot(), destination) ||
        !HasBindingSuffix(destination)) {
        result.message = "Binding destination must be a .hmodelmaterial file under Assets";
        return result;
    }
    std::string error{};
    result.sourceSaved = SaveModelMaterialBindingAsset(destination, data, error);
    if (!result.sourceSaved) {
        result.message = std::move(error);
        return result;
    }
    ModelMaterialBindingAuthoringResult importResult = ImportAndScan(destination, createMissingMeta);
    importResult.sourceSaved = true;
    importResult.validationDiagnostics = std::move(result.validationDiagnostics);
    return importResult;
}

} // namespace HIKARI::ASSETS::MODELS
