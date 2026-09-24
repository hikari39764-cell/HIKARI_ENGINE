#pragma once
#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Documents/HIKARI_EditorSourceVersion.h"
#include "Editor/History/HIKARI_DocumentSnapshotHistory.h"

namespace HIKARI::EDITOR {
template <class Traits>
class ValidatedAssetDocument {
    using Asset = typename Traits::Asset;

public:
    bool New(std::string_view name, std::string& message, bool discard = false);
    bool New(Asset draft, std::string& message, bool discard = false);
    bool Open(const AssetDatabase& database, const AssetGuid& guid, std::string& message, bool discard = false);
    bool Save(AssetDatabase& database, std::string& message, bool saveAs = false);
    bool Apply(Asset profile, std::string& message, uint64_t mergeGroup = 0);
    void SealMerge() { history_.SealMerge(); }
    bool Undo();
    bool Redo();
    bool CanUndo() const { return history_.CanUndo(); }
    bool CanRedo() const { return history_.CanRedo(); }
    bool IsDirty() const { return history_.IsDirty(); }
    uint64_t Revision() const { return revision_; }
    bool IsOpen() const { return open_; }
    const Asset& Profile() const { return profile_; }
    const AssetGuid& Guid() const { return guid_; }

private:
    void ResetHistory(bool saved);
    Asset profile_;
    AssetGuid guid_;
    std::filesystem::path path_;
    EditorSourceVersion sourceVersion_;
    DocumentSnapshotHistory<Asset> history_;
    bool open_ = false;
    uint64_t revision_ = 0;
};

template <class Traits>
void ValidatedAssetDocument<Traits>::ResetHistory(bool saved) {
    history_.Reset(saved);
    ++revision_;
    open_ = true;
}
template <class Traits>
bool ValidatedAssetDocument<Traits>::New(std::string_view name, std::string& message, bool discard) {
    Asset draft;
    draft.name = name.empty() ? Traits::title : std::string(name);
    return New(std::move(draft), message, discard);
}
template <class Traits>
bool ValidatedAssetDocument<Traits>::New(Asset draft, std::string& message, bool discard) {
    if (open_ && IsDirty() && !discard) {
        message = "Save or discard the current asset draft first.";
        return false;
    }
    if (!Traits::Normalize(draft, message))
        return false;
    profile_ = std::move(draft);
    guid_ = {};
    path_.clear();
    ResetHistory(false);
    message.clear();
    return true;
}
template <class Traits>
bool ValidatedAssetDocument<Traits>::Open(const AssetDatabase& database, const AssetGuid& guid, std::string& message,
                                          bool discard) {
    const auto* record = database.FindByGuid(guid);
    if (!record || record->type != Traits::type) {
        message = "Asset not found.";
        return false;
    }
    const auto path =
        record->sourcePath.is_absolute() ? record->sourcePath : database.GetProjectRoot() / record->sourcePath;
    const bool sameSource =
        open_ && guid == guid_ && std::filesystem::weakly_canonical(path) == std::filesystem::weakly_canonical(path_);
    if (sameSource && !discard) {
        std::string freshness;
        if (IsDirty() || sourceVersion_.CanSave(freshness)) {
            message.clear();
            return true;
        }
    }
    if (open_ && IsDirty() && !discard) {
        message = "Save or explicitly discard the current asset draft first.";
        return false;
    }
    Asset profile;
    if (!Traits::Load(path, profile, message))
        return false;
    profile_ = std::move(profile);
    guid_ = guid;
    path_ = path;
    sourceVersion_.Accept(path_);
    ResetHistory(true);
    return true;
}
template <class Traits>
bool ValidatedAssetDocument<Traits>::Save(AssetDatabase& database, std::string& message, bool saveAs) {
    if (!open_) {
        message = "No asset document is open.";
        return false;
    }
    if (!saveAs && !path_.empty()) {
        const auto source = std::filesystem::weakly_canonical(path_);
        const auto relative = source.lexically_relative(std::filesystem::weakly_canonical(database.GetAssetsRoot()));
        const auto* record = guid_.IsValid() ? database.FindByGuid(guid_) : nullptr;
        if (relative.empty() || relative.is_absolute() || *relative.begin() == ".." ||
            (guid_.IsValid() &&
             (!record || record->type != Traits::type ||
              std::filesystem::weakly_canonical(database.GetProjectRoot() / record->sourcePath) != source))) {
            message = "This asset draft belongs to another project. Use "
                      "Save As to create a copy.";
            return false;
        }
        if (!sourceVersion_.CanSave(message))
            return false;
    }
    auto path = path_;
    const bool create = saveAs || path.empty();
    if (create) {
        const auto directory = database.GetAssetsRoot() / Traits::directory;
        path = directory / (std::string(Traits::stem) + Traits::extension);
        for (unsigned suffix = 2; std::filesystem::exists(path); ++suffix)
            path = directory / (std::string(Traits::stem) + "_" + std::to_string(suffix) + Traits::extension);
    }
    if (!Traits::Save(path, profile_, message))
        return false;
    // ソース公開と import 完了は別段階。途中失敗でも同じファイルで再試行する。
    path_ = path;
    if (create)
        guid_ = {};
    sourceVersion_.Accept(path_);
    if (!database.ScanAssets(!guid_.IsValid())) {
        message = "Asset saved, but asset scan failed.";
        return false;
    }
    auto* record = database.FindByPath(path);
    if (!record || record->type != Traits::type || !record->guid.IsValid()) {
        message = "Asset saved, but asset metadata is missing.";
        return false;
    }
    guid_ = record->guid;
    record->displayName = profile_.name;
    record->meta.displayName = profile_.name;
    if (!database.WriteMeta(*record)) {
        message = "Asset saved, but metadata update failed.";
        return false;
    }
    if (!database.ImportAsset(guid_)) {
        message = "Asset saved, but asset validation failed.";
        return false;
    }
    history_.MarkSaved();
    message.clear();
    return true;
}
template <class Traits>
bool ValidatedAssetDocument<Traits>::Apply(Asset profile, std::string& message, uint64_t mergeGroup) {
    if (!Traits::Normalize(profile, message))
        return false;
    if (Traits::Write(profile) == Traits::Write(profile_))
        return true;
    history_.Record(profile_, profile, mergeGroup);
    profile_ = std::move(profile);
    ++revision_;
    open_ = true;
    return true;
}
template <class Traits>
bool ValidatedAssetDocument<Traits>::Undo() {
    if (!history_.Undo(profile_))
        return false;
    ++revision_;
    return true;
}
template <class Traits>
bool ValidatedAssetDocument<Traits>::Redo() {
    if (!history_.Redo(profile_))
        return false;
    ++revision_;
    return true;
}

} // namespace HIKARI::EDITOR
