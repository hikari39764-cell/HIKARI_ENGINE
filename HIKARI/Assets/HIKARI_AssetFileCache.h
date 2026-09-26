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

    std::shared_ptr<const Asset> Load(const AssetGuid& guid, std::string& error) const {
        if (!database_ || !guid.IsValid()) {
            error = "No " + std::string(ASSETS::SEMANTICS::ToString(Type)) + " asset is bound.";
            return {};
        }
        if (revision_ != database_->GetContentRevision()) {
            entries_.clear();
            revision_ = database_->GetContentRevision();
        }
        if (const auto found = entries_.find(guid.value); found != entries_.end()) {
            error = found->second.error;
            return found->second.asset;
        }
        Entry entry;
        const AssetRecord* record = database_->FindByGuid(guid);
        if (!record || record->type != Type || !record->lastImportSucceeded) {
            entry.error =
                std::string(ASSETS::SEMANTICS::ToString(Type)) + " asset is missing or failed import: " + guid.value;
        } else {
            auto asset = std::make_shared<Asset>();
            const auto path = record->sourcePath.is_absolute() ? record->sourcePath
                                                               : database_->GetProjectRoot() / record->sourcePath;
            if (LoadSource(path, *asset, entry.error)) {
                entry.asset = std::move(asset);
            }
        }
        error = entry.error;
        auto asset = entry.asset;
        entries_.emplace(guid.value, std::move(entry));
        return asset;
    }

private:
    struct Entry {
        std::shared_ptr<const Asset> asset;
        std::string error;
    };
    const AssetDatabase* database_ = nullptr;
    mutable uint64_t revision_ = 0;
    mutable std::unordered_map<std::string, Entry> entries_;
};

} // namespace HIKARI
