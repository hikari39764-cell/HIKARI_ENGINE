#include "Editor/Assets/HIKARI_AssetTrashService.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <system_error>
#include <unordered_set>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/HIKARI_AssetUsageAnalyzer.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Assets/Material/HIKARI_MaterialDependencies.h"
#include "Assets/Models/MaterialBindings/HIKARI_ModelMaterialBindingAsset.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"

namespace HIKARI::EDITOR {
namespace {

bool IsInsideProject(const std::filesystem::path& projectRoot, const std::filesystem::path& path) {
    std::error_code ec{};
    const std::filesystem::path relative =
        std::filesystem::relative(path.lexically_normal(), projectRoot.lexically_normal(), ec);
    if (ec || relative.empty() || relative.is_absolute()) {
        return false;
    }
    return relative.begin() == relative.end() || relative.begin()->string() != "..";
}

bool MoveFile(const std::filesystem::path& source, const std::filesystem::path& destination, std::string& outMessage) {
    std::error_code ec{};
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
        outMessage = "could not create trash directory: " + ec.message();
        return false;
    }
    std::filesystem::rename(source, destination, ec);
    if (ec) {
        outMessage = "could not move asset to trash: " + ec.message();
        return false;
    }
    return true;
}

bool SourceContainsGuid(const std::filesystem::path& sourcePath, const AssetGuid& guid) {
    std::ifstream stream(sourcePath, std::ios::binary);
    if (!stream) {
        return false;
    }
    std::ostringstream contents{};
    contents << stream.rdbuf();
    return contents.str().find(guid.value) != std::string::npos;
}

void AddTrashReference(const AssetRecord& owner, std::string role, AssetTrashReferenceAnalysis& analysis,
                       std::unordered_set<std::string>& uniqueReferences) {
    const std::string key = owner.guid.value + "\x1f" + role;
    if (!uniqueReferences.emplace(key).second) {
        return;
    }
    analysis.references.push_back(AssetTrashReference{owner.guid, owner.sourcePath, std::move(role)});
}

} // namespace

AssetTrashReferenceAnalysis AnalyzeAssetTrashReferences(const AssetDatabase& database, const AssetGuid& guid) {
    AssetTrashReferenceAnalysis analysis{};
    if (!guid.IsValid()) {
        return analysis;
    }
    std::unordered_set<std::string> uniqueReferences{};

    for (const AssetRecord* owner : database.CollectByType(AssetType::Material)) {
        if (owner == nullptr || owner->guid == guid) {
            continue;
        }
        const std::filesystem::path sourcePath = database.ResolveSourcePath(*owner);
        MaterialAssetData source{};
        std::string error{};
        if (LoadMaterialAssetData(sourcePath, source, error)) {
            for (const MaterialAssetDependency& dependency : CollectMaterialAssetDependencies(source)) {
                if (dependency.assetGuid == guid) {
                    AddTrashReference(*owner, "Material " + dependency.role, analysis, uniqueReferences);
                }
            }
        } else if (SourceContainsGuid(sourcePath, guid)) {
            AddTrashReference(*owner, "unreadable Material source reference", analysis, uniqueReferences);
        }
    }

    for (const AssetRecord* owner : database.CollectByType(AssetType::ModelMaterialBinding)) {
        if (owner == nullptr || owner->guid == guid) {
            continue;
        }
        const std::filesystem::path sourcePath = database.ResolveSourcePath(*owner);
        ASSETS::MODELS::ModelMaterialBindingAssetData source{};
        std::string error{};
        if (ASSETS::MODELS::LoadModelMaterialBindingAsset(sourcePath, source, error)) {
            if (source.modelAssetGuid == guid) {
                AddTrashReference(*owner, "Material Set model", analysis, uniqueReferences);
            }
            for (const auto& binding : source.bindings) {
                if (binding.materialAssetGuid == guid) {
                    AddTrashReference(*owner, "Material Set part", analysis, uniqueReferences);
                }
            }
        } else if (SourceContainsGuid(sourcePath, guid)) {
            AddTrashReference(*owner, "unreadable Material Set source reference", analysis, uniqueReferences);
        }
    }

    SceneSerializer sceneSerializer{};
    for (const AssetRecord* owner : database.CollectByType(AssetType::Scene)) {
        if (owner == nullptr || owner->guid == guid) {
            continue;
        }
        const std::filesystem::path sourcePath = database.ResolveSourcePath(*owner);
        SceneDocument document{};
        if (sceneSerializer.LoadFromFile(sourcePath.string(), document)) {
            if (AnalyzeAssetUsage(document, database).IsUsed(guid)) {
                AddTrashReference(*owner, "Scene asset dependency", analysis, uniqueReferences);
            }
        } else if (SourceContainsGuid(sourcePath, guid)) {
            AddTrashReference(*owner, "unreadable Scene source reference", analysis, uniqueReferences);
        }
    }
    return analysis;
}

AssetTrashResult MoveAssetToProjectTrash(AssetDatabase& database, const AssetGuid& guid) {
    AssetTrashResult result{};
    const AssetRecord* record = database.FindByGuid(guid);
    if (record == nullptr || !record->guid.IsValid()) {
        result.message = "asset is no longer present in the database";
        return result;
    }
    const AssetTrashReferenceAnalysis references = AnalyzeAssetTrashReferences(database, guid);
    if (references.IsReferenced()) {
        const AssetTrashReference& first = references.references.front();
        result.message =
            "asset is still referenced by " + first.ownerSourcePath.generic_string() + " (" + first.role + ")";
        if (references.references.size() > 1u) {
            result.message += " and " + std::to_string(references.references.size() - 1u) + " other asset(s)";
        }
        return result;
    }

    const std::filesystem::path projectRoot = database.GetProjectRoot().lexically_normal();
    const std::filesystem::path source = record->sourcePath.is_absolute()
                                             ? record->sourcePath.lexically_normal()
                                             : (projectRoot / record->sourcePath).lexically_normal();
    if (!IsInsideProject(projectRoot, source)) {
        result.message = "refusing to move an asset outside the project";
        return result;
    }

    std::error_code ec{};
    if (!std::filesystem::is_regular_file(source, ec) || ec) {
        result.message = "asset source file is missing";
        return result;
    }

    const auto ticks =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    result.trashDirectory =
        database.GetLibraryRoot() / "Trash" / ("asset_" + std::to_string(ticks) + "_" + guid.value.substr(0u, 8u));
    const std::filesystem::path trashSource = result.trashDirectory / "Source" / record->sourcePath.filename();
    if (!MoveFile(source, trashSource, result.message)) {
        return result;
    }

    const std::filesystem::path meta = record->metaPath;
    if (!meta.empty() && std::filesystem::is_regular_file(meta, ec) && !ec) {
        const std::filesystem::path trashMeta = result.trashDirectory / "Meta" / meta.filename();
        if (!MoveFile(meta, trashMeta, result.message)) {
            std::error_code rollback{};
            std::filesystem::rename(trashSource, source, rollback);
            if (rollback) {
                result.message += "; source rollback also failed: " + rollback.message();
            }
            return result;
        }
    }

    result.success = true;
    result.message = "asset moved to Library/Trash";
    return result;
}

} // namespace HIKARI::EDITOR
