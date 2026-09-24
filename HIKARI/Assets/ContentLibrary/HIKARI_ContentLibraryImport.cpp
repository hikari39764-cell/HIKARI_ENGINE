#include "Assets/ContentLibrary/HIKARI_ContentLibraryInternal.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Assets/Vfx/HIKARI_VfxBuiltinTextureCatalog.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/Paths/HIKARI_ProjectPath.h"
#include <map>
#include <set>

namespace HIKARI::ASSETS::LIBRARY {
namespace {
// 失敗時の空ディレクトリだけを回収。既存ファイルとディレクトリは所有しない。
struct InstallDirectories {
    std::vector<std::filesystem::path> paths;
    void Ensure(const std::filesystem::path& path) {
        if (std::filesystem::exists(path))
            return;
        Ensure(path.parent_path());
        std::filesystem::create_directory(path);
        paths.push_back(path);
    }
    ~InstallDirectories() {
        for (auto it = paths.rbegin(); it != paths.rend(); ++it) {
            std::error_code ignored;
            std::filesystem::remove(*it, ignored);
        }
    }
};
} // namespace
bool ImportContentPackage(AssetDatabase& database, const std::filesystem::path& manifestPath, ContentImportResult& out,
                          std::string& message) {
    using namespace INTERNAL;
    out = {};
    std::string context = "Read package";
    try {
        if (database.IsCookedContent() || database.GetQueuedImportStatus().active) {
            message = "Finish the active import before installing content into a source project";
            return false;
        }
        nlohmann::json manifest;
        ContentPackageInfo info;
        if (!ReadManifest(manifestPath, manifest, info, message))
            return false;
        std::string manifestHash;
        if (!FileHash(manifestPath, manifestHash, message))
            return false;
        const auto builtinList = manifest.value("requiredBuiltins", nlohmann::json::array());
        if (!builtinList.is_array()) {
            message = "Invalid built-in dependency list";
            return false;
        }
        std::set<std::string> requiredBuiltins;
        for (const auto& id : builtinList) {
            if (!id.is_string() || !VFX::IsVfxBuiltinTextureAssetId(id.get<std::string>())) {
                message = "Package needs built-in content unavailable in this engine";
                return false;
            }
            requiredBuiltins.insert(id.get<std::string>());
        }
        const auto relativeRoot = std::filesystem::path("Assets/Shared") / ("pkg_" + info.id) / ("v_" + info.version);
        const auto destination = database.GetProjectRoot() / relativeRoot;
        const auto receiptPath = database.GetProjectRoot() / "ProjectSettings/ContentPackages" / ("pkg_" + info.id) /
                                 ("v_" + info.version + ".json");
        if (!IsWithin(database.GetAssetsRoot(), destination) ||
            !IsWithin(database.GetProjectRoot() / "ProjectSettings", receiptPath)) {
            message = "Content destination escapes the project";
            return false;
        }
        if (std::filesystem::exists(receiptPath)) {
            nlohmann::json receipt;
            if (!SERIALIZATION::JSON::ReadJsonFile(receiptPath, receipt, &message))
                return false;
            if (receipt.at("schema") != "hikari.content_install" || receipt.at("version") != 1 ||
                receipt.at("id") != info.id || receipt.at("revision") != info.version ||
                !receipt.at("assets").is_array() || receipt.at("assets").size() != info.assetCount ||
                !receipt.at("roots").is_array() || receipt.at("roots").size() != info.roots.size()) {
                message = "Invalid project content receipt; local files were left unchanged";
                return false;
            }
            if (receipt.at("manifestHash") != manifestHash) {
                message = "This revision changed in the library. Publish it as a new revision";
                return false;
            }
            ContentImportResult reused;
            std::map<std::string, std::string> installedIds;
            std::set<std::string> projectIds;
            for (const auto& item : receipt.at("assets")) {
                AssetGuid guid{item.at("guid").get<std::string>()};
                if (!projectIds.insert(guid.value).second ||
                    !installedIds.emplace(item.at("origin").get<std::string>(), guid.value).second) {
                    message = "Duplicate identity in content receipt";
                    return false;
                }
                const auto* record = database.FindByGuid(guid);
                if (!record || record->duplicateGuid || !record->sourceExists || !record->metaExists ||
                    !IsWithin(destination,
                              PROJECT_PATHS::ResolveProjectPath(database.GetProjectRoot(), record->sourcePath))) {
                    message = "Installed content is incomplete; local files were left unchanged";
                    return false;
                }
                reused.assets.push_back(guid);
            }
            for (const auto& asset : manifest.at("assets")) {
                if (!installedIds.contains(asset.at("guid").get<std::string>())) {
                    message = "Content receipt does not cover this package";
                    return false;
                }
            }
            for (size_t index = 0; index < info.roots.size(); ++index) {
                const auto id = receipt.at("roots").at(index).get<std::string>();
                if (id != installedIds.at(info.roots[index].value)) {
                    message = "Content receipt root mapping is invalid";
                    return false;
                }
                reused.roots.push_back({id});
            }
            reused.reused = true;
            out = std::move(reused);
            message = "Already installed; existing project edits were preserved";
            return true;
        }
        if (std::filesystem::exists(destination)) {
            message = "Content destination already exists without a receipt; no files replaced";
            return false;
        }
        std::map<std::string, AssetGuid> identities;
        std::set<std::string> generated;
        for (const auto& asset : manifest.at("assets")) {
            context = "Allocate project identity";
            AssetGuid guid;
            do {
                guid = GenerateAssetGuid();
            } while (database.FindByGuid(guid) || !generated.insert(guid.value).second);
            identities.emplace(asset.at("guid").get<std::string>(), std::move(guid));
        }
        StagingDirectory stage(database.GetLibraryRoot());
        std::vector<IO::FileReplacementOperation> writes;
        ContentImportResult imported;
        nlohmann::json receipt = {{"schema", "hikari.content_install"},
                                  {"version", 1},
                                  {"id", info.id},
                                  {"revision", info.version},
                                  {"manifestHash", manifestHash},
                                  {"license", info.license},
                                  {"assets", nlohmann::json::array()},
                                  {"roots", nlohmann::json::array()}};
        for (const auto& asset : manifest.at("assets")) {
            context = "Import " + asset.at("path").get<std::string>();
            std::filesystem::path sourcePath;
            if (!ResolveMember(manifestPath.parent_path(), asset.at("path").get<std::string>(), sourcePath, message))
                return false;
            std::string hash;
            const auto origin = asset.at("guid").get<std::string>();
            const auto guid = identities.at(origin);
            const auto relative = relativeRoot / guid.value / sourcePath.filename();
            const auto finalPath = database.GetProjectRoot() / relative;
            const auto stagedSource = stage.path / guid.value / sourcePath.filename();
            const auto metaPath = database.GetMetaPathForSource(relative);
            const auto stagedMeta = stage.path / (guid.value + ".meta.json");
            if (!IsWithin(database.GetSourceMetaRoot(), metaPath) || std::filesystem::exists(metaPath)) {
                message = "Content metadata destination already exists or is outside its root";
                return false;
            }
            auto meta = asset.at("meta");
            const auto type = SEMANTICS::ParseAssetType(meta.at("type").get<std::string>());
            // 同じ検証済みバイト列を解析する。共有側が編集中でも読み替わらない。
            std::filesystem::create_directories(stagedSource.parent_path());
            std::filesystem::copy_file(sourcePath, stagedSource);
            if (!FileHash(stagedSource, hash, message))
                return false;
            if (hash != asset.at("sha256").get<std::string>()) {
                message = "Content file hash mismatch";
                return false;
            }
            Source source;
            if (!LoadSource(stagedSource, type, source, message))
                return false;
            bool missing = false;
            VisitReferences(source, [&](AssetGuid& reference) {
                if (!reference.IsValid() || requiredBuiltins.contains(reference.value))
                    return;
                const auto found = identities.find(reference.value);
                if (found == identities.end()) {
                    missing = true;
                    message = "Missing packaged dependency: " + reference.value;
                } else
                    reference = found->second;
            });
            if (missing)
                return false;
            if (!std::holds_alternative<std::monostate>(source) && !SaveSource(stagedSource, source, message))
                return false;
            meta["guid"] = guid.value;
            meta["sourcePath"] = TEXT::PathToUtf8(relative);
            if (!SERIALIZATION::JSON::WriteJsonFile(stagedMeta, meta, &message))
                return false;
            writes.push_back({finalPath, stagedSource});
            writes.push_back({metaPath, stagedMeta});
            imported.assets.push_back(guid);
            if (!FileHash(stagedSource, hash, message))
                return false;
            receipt["assets"].push_back({{"origin", origin},
                                         {"guid", guid.value},
                                         {"path", TEXT::PathToUtf8(relative)},
                                         {"installedHash", hash}});
        }
        for (const auto& root : info.roots) {
            imported.roots.push_back(identities.at(root.value));
            receipt["roots"].push_back(identities.at(root.value).value);
        }
        const auto stagedReceipt = stage.path / "receipt.json";
        if (!SERIALIZATION::JSON::WriteJsonFile(stagedReceipt, receipt, &message))
            return false;
        writes.push_back({receiptPath, stagedReceipt});
        InstallDirectories directories;
        for (const auto& write : writes)
            directories.Ensure(write.finalPath.parent_path());
        if (!IO::CommitFileReplacementTransaction(writes, message))
            return false;
        context = "Scan installed sources";
        bool scanned = false;
        try {
            scanned = database.ScanAssets(false);
        } catch (const std::exception& error) {
            message = error.what();
        }
        if (!scanned) {
            // 既存資産には触れていない。今回追加したファイルのみを共有トランザクションで戻す。
            std::vector<IO::FileReplacementOperation> rollback;
            for (const auto& write : writes)
                rollback.push_back({write.finalPath, {}, true});
            std::string rollbackMessage;
            (void)IO::CommitFileReplacementTransaction(rollback, rollbackMessage);
            (void)database.ScanAssets(false);
            message = "Project scan failed after content import. " + rollbackMessage;
            return false;
        }
        out = std::move(imported);
        message = "Imported " + info.name + "; source assets are ready to cook";
        return true;
    } catch (const std::exception& error) {
        message = context + ": " + error.what();
        return false;
    }
}
} // namespace HIKARI::ASSETS::LIBRARY
