#include "Editor/Assets/Models/HIKARI_ModelMaterialSlotCatalog.h"

#include <algorithm>
#include <filesystem>
#include <utility>

#include "Assets/Formats/HIKARI_HmodelFormat.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Assets/Models/MaterialBindings/HIKARI_ModelMaterialBindingAsset.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Project/Paths/HIKARI_ProjectPath.h"

namespace HIKARI::EDITOR {

std::span<const ModelMaterialSlotAuthoringView> ModelMaterialSlotCatalog::ResolveMaterialSlots(
    const AssetDatabase& database, const AssetGuid& modelGuid, std::string& outDiagnostic) {
    SynchronizeRevision(database);
    if (!modelGuid.IsValid()) {
        outDiagnostic = "Select a model asset to inspect its material slots.";
        return {};
    }

    const std::string key = modelGuid.value;
    auto found = entries_.find(key);
    if (found == entries_.end()) {
        found = entries_.emplace(key, BuildEntry(database, modelGuid)).first;
    }

    outDiagnostic = found->second.diagnostic;
    return found->second.slots;
}

std::span<const ModelMaterialSetAuthoringView> ModelMaterialSlotCatalog::ResolveCompatibleMaterialSets(
    const AssetDatabase& database, const AssetGuid& modelGuid, std::string& outDiagnostic) {
    SynchronizeRevision(database);
    if (!modelGuid.IsValid()) {
        outDiagnostic = "Select a model before choosing a Material Set.";
        return {};
    }

    const std::string key = modelGuid.value;
    auto found = entries_.find(key);
    if (found == entries_.end()) {
        found = entries_.emplace(key, BuildEntry(database, modelGuid)).first;
    }
    outDiagnostic = found->second.materialSetDiagnostic;
    return found->second.materialSets;
}

void ModelMaterialSlotCatalog::Clear() {
    entries_.clear();
    database_ = nullptr;
    contentRevision_ = 0u;
}

void ModelMaterialSlotCatalog::SynchronizeRevision(const AssetDatabase& database) {
    const uint64_t revision = database.GetContentRevision();
    if (database_ == &database && contentRevision_ == revision) {
        return;
    }
    entries_.clear();
    database_ = &database;
    contentRevision_ = revision;
}

ModelMaterialSlotCatalog::Entry ModelMaterialSlotCatalog::BuildEntry(const AssetDatabase& database,
                                                                     const AssetGuid& modelGuid) const {
    Entry entry{};
    const AssetRecord* record = database.FindByGuid(modelGuid);
    if (record == nullptr || record->type != AssetType::Model) {
        entry.diagnostic = "Model asset is not present in the project database.";
        return entry;
    }

    const std::string artifactPath =
        ASSETS::SEMANTICS::FindAssetArtifactPath(*record, ASSETS::SEMANTICS::AssetArtifactKind::MainModel, true);
    if (artifactPath.empty()) {
        entry.diagnostic = "Model has no HMODEL artifact. Import or reimport it first.";
        return entry;
    }

    const std::filesystem::path modelPath =
        PROJECT_PATHS::ResolveProjectPath(database.GetProjectRoot(), std::filesystem::path(artifactPath));
    ModelAsset model{};
    std::string readMessage{};
    if (!ReadHmodelFile(modelPath, model, readMessage)) {
        entry.diagnostic = readMessage.empty() ? "Failed to read the model material-slot catalog."
                                               : "Failed to read model slots: " + readMessage;
        return entry;
    }

    entry.slots.reserve(model.materials.size());
    for (size_t index = 0u; index < model.materials.size(); ++index) {
        const MaterialAsset& material = model.materials[index];
        entry.slots.push_back(ModelMaterialSlotAuthoringView{static_cast<uint32_t>(index), material.slotId,
                                                             material.name.empty() ? "Unnamed" : material.name,
                                                             material.baseColorTexture.textureIndex >= 0});
    }
    if (entry.slots.empty()) {
        entry.diagnostic = "Imported model contains no material slots.";
    }

    for (const AssetRecord* materialSetRecord : database.CollectByType(AssetType::ModelMaterialBinding)) {
        if (materialSetRecord == nullptr || !materialSetRecord->guid.IsValid()) {
            continue;
        }
        ASSETS::MODELS::ModelMaterialBindingAssetData data{};
        std::string loadMessage{};
        const std::filesystem::path sourcePath =
            PROJECT_PATHS::ResolveProjectPath(database.GetProjectRoot(), materialSetRecord->sourcePath);
        if (!ASSETS::MODELS::LoadModelMaterialBindingAsset(sourcePath, data, loadMessage) ||
            data.modelAssetGuid != modelGuid) {
            continue;
        }
        entry.materialSets.push_back(ModelMaterialSetAuthoringView{
            materialSetRecord->guid, data.displayName.empty() ? materialSetRecord->displayName : data.displayName});
    }
    std::sort(entry.materialSets.begin(), entry.materialSets.end(),
              [](const auto& left, const auto& right) { return left.name < right.name; });
    if (entry.materialSets.empty()) {
        entry.materialSetDiagnostic = "No Material Set has been authored for this model yet.";
    }
    return entry;
}

} // namespace HIKARI::EDITOR
