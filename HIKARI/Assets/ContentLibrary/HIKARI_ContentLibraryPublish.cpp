#include "Assets/ContentLibrary/HIKARI_ContentLibraryInternal.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Vfx/HIKARI_VfxBuiltinTextureCatalog.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/Paths/HIKARI_ProjectPath.h"
#include <map>
#include <set>

namespace HIKARI::ASSETS::LIBRARY {
    bool PublishContentPackage(const AssetDatabase& database,
        std::span<const AssetGuid> roots, const std::filesystem::path& libraryRoot,
        const ContentPackageInfo& info, std::string& message) {
        using namespace INTERNAL;
        try {
            if (database.IsCookedContent() || libraryRoot.empty() || roots.empty() ||
                !SafeKey(info.id) || !SafeKey(info.version) || info.name.empty()) {
                message = "Publishing requires source assets, a library folder, name, ID and revision";
                return false;
            }
            const auto destination = libraryRoot / ("pkg_" + info.id) / ("v_" + info.version);
            if (!IsWithin(libraryRoot, destination) || std::filesystem::exists(destination)) {
                message = "This package revision already exists; publish a new revision";
                return false;
            }
            std::map<std::string, const AssetRecord*> closure;
            std::vector<AssetGuid> pending(roots.begin(), roots.end());
            std::set<std::string> requiredBuiltins;
            while (!pending.empty()) {
                const auto guid = pending.back();
                pending.pop_back();
                if (!guid.IsValid() || closure.contains(guid.value)) continue;
                if (VFX::IsVfxBuiltinTextureAssetId(guid.value)) {
                    requiredBuiltins.insert(guid.value);
                    continue;
                }
                const auto* record = database.FindByGuid(guid);
                if (!record || record->duplicateGuid || !record->sourceExists || !record->metaExists) {
                    message = "Content dependency is missing or ambiguous: " + guid.value;
                    return false;
                }
                const auto path = PROJECT_PATHS::ResolveProjectPath(database.GetProjectRoot(), record->sourcePath);
                if (!IsWithin(database.GetAssetsRoot(), path)) {
                    message = "Content source is outside project Assets";
                    return false;
                }
                Source source;
                if (!LoadSource(path, record->type, source, message)) return false;
                closure.emplace(guid.value, record);
                if (closure.size() > 4096) { message = "Content package exceeds 4096 assets"; return false; }
                VisitReferences(source, [&](AssetGuid& dependency) { pending.push_back(dependency); });
            }
            StagingDirectory stage(libraryRoot);
            nlohmann::json manifest = {
                {"schema", "hikari.content_package"}, {"version", 1},
                {"id", info.id}, {"revision", info.version}, {"name", info.name},
                {"category", info.category}, {"description", info.description}, {"license", info.license},
                {"roots", nlohmann::json::array()}, {"assets", nlohmann::json::array()},
                {"requiredBuiltins", requiredBuiltins}
            };
            std::set<std::string> uniqueRoots;
            for (const auto& root : roots) {
                if (!closure.contains(root.value)) { message = "Package roots must be project assets"; return false; }
                if (uniqueRoots.insert(root.value).second) manifest["roots"].push_back(root.value);
            }
            for (const auto& [id, record] : closure) {
                const auto sourcePath = PROJECT_PATHS::ResolveProjectPath(database.GetProjectRoot(), record->sourcePath);
                const auto relative = std::filesystem::path("files") / id / sourcePath.filename();
                const auto staged = stage.path / relative;
                std::filesystem::create_directories(staged.parent_path());
                std::filesystem::copy_file(sourcePath, staged);
                nlohmann::json meta;
                if (!SERIALIZATION::JSON::ReadJsonFile(record->metaPath, meta, &message)) return false;
                meta["sourcePath"] = TEXT::PathToUtf8(relative);
                std::string hash;
                if (!FileHash(staged, hash, message)) return false;
                manifest["assets"].push_back({{"guid", id}, {"path", TEXT::PathToUtf8(relative)},
                    {"sha256", hash}, {"meta", std::move(meta)}});
            }
            const auto manifestPath = stage.path / "package.hikari-content.json";
            if (!SERIALIZATION::JSON::WriteJsonFile(manifestPath, manifest, &message)) return false;
            ContentPackageInfo checked;
            if (!ReadContentPackage(manifestPath, checked, message)) return false;
            std::filesystem::create_directories(destination.parent_path());
            std::filesystem::rename(stage.path, destination);
            message = "Published " + info.name + " (" + std::to_string(closure.size()) + " assets)";
            return true;
        } catch (const std::exception& error) {
            message = error.what();
            return false;
        }
    }
}
