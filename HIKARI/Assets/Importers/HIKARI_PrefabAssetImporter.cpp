#include "Assets/Importers/HIKARI_PrefabAssetImporter.h"

#include <unordered_map>

#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetUsageAnalyzer.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Project/Paths/HIKARI_ProjectPath.h"
#include "Scene/Prefab/HIKARI_PrefabDocument.h"
#include "Scene/Prefab/HIKARI_PrefabSerializer.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"

namespace HIKARI {

ASSETS::SEMANTICS::AssetImporterKind PrefabAssetImporter::GetImporterKind() const noexcept {
    return ASSETS::SEMANTICS::AssetImporterKind::Prefab;
}

AssetMeta PrefabAssetImporter::CreateDefaultMeta(const std::filesystem::path& sourcePath, const AssetGuid& guid) const {
    return ASSETS::SEMANTICS::MakeBaseAssetMeta(sourcePath, guid, GetImporterKind());
}

AssetImportResult PrefabAssetImporter::Import(const AssetRecord& record, const AssetImportContext& context) {
    AssetImportResult result{};
    if (!ASSETS::SEMANTICS::IsAssetSourceForImporter(record.sourcePath, GetImporterKind())) {
        result.message = "Prefab importer received an unsupported source path.";
        return result;
    }
    const std::filesystem::path sourcePath = record.sourcePath.is_absolute()
                                                 ? record.sourcePath
                                                 : (context.projectRoot / record.sourcePath).lexically_normal();
    PrefabDocument prefab{};
    std::string error{};
    if (!PrefabSerializer{}.LoadFromFile(sourcePath, prefab, error)) {
        result.message = "Prefab validation failed: " + error;
        return result;
    }

    std::unordered_map<uint64_t, uint64_t> localIds{};
    localIds.emplace(prefab.rootObject.id.value, prefab.rootObject.id.value);
    for (const SceneObjectData& object : prefab.descendants) {
        localIds.emplace(object.id.value, object.id.value);
    }
    uint32_t externalReferences = 0u;
    const auto validateReferences = [&localIds, &externalReferences](const SceneObjectData& object) {
        for (const SceneComponentData& component : object.components) {
            nlohmann::json properties = component.properties;
            externalReferences += SCENE::SERIALIZATION::RemapSceneObjectReferences(properties, localIds);
        }
    };
    validateReferences(prefab.rootObject);
    for (const SceneObjectData& object : prefab.descendants) {
        validateReferences(object);
    }
    if (externalReferences != 0u) {
        result.message = "Prefab has external scene-object references; include the targets or clear those bindings.";
        return result;
    }

    SceneDocument prefabObjects{};
    prefabObjects.objects.reserve(prefab.descendants.size() + 1u);
    prefabObjects.objects.push_back(prefab.rootObject);
    prefabObjects.objects.insert(prefabObjects.objects.end(), prefab.descendants.begin(), prefab.descendants.end());
    if (context.assetDatabase == nullptr) {
        result.message = "Prefab dependency analysis requires the project asset database.";
        return result;
    }
    const AssetUsageSummary usage =
        AnalyzeAssetUsage(prefabObjects, *context.assetDatabase, AssetUsageScope::ObjectComponents);
    if (!usage.missingReferences.empty()) {
        const AssetMissingReference& missing = usage.missingReferences.front();
        result.message =
            "Prefab dependency is missing: " + missing.assetId + " (" + missing.owner + ", " + missing.role + ").";
        return result;
    }
    result.dependencies.reserve(usage.usedGuids.size());
    for (const std::string& guid : usage.usedGuids) {
        result.dependencies.push_back(AssetDependencyDesc{AssetGuid{guid}, {}, "Prefab.ComponentAsset"});
    }

    const std::filesystem::path artifactPath = context.importedDirectory / "prefab.hprefabc";
    if (!PrefabSerializer{}.SaveToFile(context.StageFile(artifactPath), prefab, error)) {
        result.message = "Prefab runtime artifact write failed: " + error;
        return result;
    }
    nlohmann::json objects = nlohmann::json::array();
    const auto appendObject = [&objects](const SceneObjectData& object) {
        objects.push_back({{"id", object.id.value}, {"name", object.name}});
    };
    appendObject(prefab.rootObject);
    for (const SceneObjectData& object : prefab.descendants) {
        appendObject(object);
    }
    result.diagnosticsJson =
        nlohmann::json{
            {"valid", true},
            {"objectCount", objects.size()},
            {"objects", std::move(objects)},
        }
            .dump(2);
    result.artifacts.push_back(ASSETS::SEMANTICS::MakeAssetArtifact(
        ASSETS::SEMANTICS::AssetArtifactKind::PrefabRuntime,
        PROJECT_PATHS::MakeProjectRelativeString(context.projectRoot, artifactPath)));
    result.success = true;
    result.message = "Prefab validated and runtime artifact published.";
    return result;
}

} // namespace HIKARI
