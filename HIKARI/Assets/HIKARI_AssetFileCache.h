#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"

namespace HIKARI {

// 導入済みのプロジェクト資産ソースを GUID ごとに読んで保持する。資産の内容バージョンが
// 変わった時だけ捨てるので、毎フレームのファイル探索や JSON 解析を避けられる。
template <class Asset, AssetType Type, bool (*LoadSource)(const std::filesystem::path&, Asset&, std::string&)>
class AssetFileCache {
public:
    void Configure(const AssetDatabase* database) {
        database_ = database;
        revision_ = 0;
        entries_.clear();
    }

    std::shared_ptr<const Asset> Load(const AssetGuid& guid, std::string& error) const { return Load(guid, {}, error); }

    // Some runtime assets are loaded from a declared cooked artifact rather
    // than the authored source path. The selected path still shares the
    // normal GUID + content-revision cache and is borrowed only for lookup.
    std::shared_ptr<const Asset> Load(const AssetGuid& guid, const std::filesystem::path& loadPath, std::string& error,
                                      bool allowUnimportedSource = false) const {
        if (!database_ || !guid.IsValid()) {
            error = "No " + std::string(ASSETS::SEMANTICS::ToString(Type)) + " asset is bound.";
            return {};
        }
        if (revision_ != database_->GetContentRevision()) {
            entries_.clear();
            revision_ = database_->GetContentRevision();
        }
        Entry entry;
        const AssetRecord* record = database_->FindByGuid(guid);
        if (!record || record->type != Type || (!record->lastImportSucceeded && !allowUnimportedSource)) {
            entry.error =
                std::string(ASSETS::SEMANTICS::ToString(Type)) + " asset is missing or failed import: " + guid.value;
        } else {
            const auto selectedPath = loadPath.empty() ? record->sourcePath : loadPath;
            entry.path = (selectedPath.is_absolute() ? selectedPath : database_->GetProjectRoot() / selectedPath)
                             .lexically_normal();
            if (const auto found = entries_.find(guid.value);
                found != entries_.end() && found->second.path == entry.path) {
                error = found->second.error;
                return found->second.asset;
            }
            auto asset = std::make_shared<Asset>();
            if (LoadSource(entry.path, *asset, entry.error)) {
                entry.asset = std::move(asset);
            }
        }
        error = entry.error;
        auto asset = entry.asset;
        entries_.insert_or_assign(guid.value, std::move(entry));
        return asset;
    }

private:
    struct Entry {
        std::shared_ptr<const Asset> asset;
        std::string error;
        std::filesystem::path path{};
    };
    const AssetDatabase* database_ = nullptr;
    mutable uint64_t revision_ = 0;
    mutable std::unordered_map<std::string, Entry> entries_;
};

} // namespace HIKARI
