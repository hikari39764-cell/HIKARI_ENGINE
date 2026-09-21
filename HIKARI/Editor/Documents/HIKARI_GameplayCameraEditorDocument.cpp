#include "Editor/Documents/HIKARI_GameplayCameraEditorDocument.h"
#include "Assets/HIKARI_AssetDatabase.h"

namespace HIKARI::EDITOR {
void GameplayCameraEditorDocument::ResetHistory(bool saved) {
    history_.clear();
    cursor_ = 0;
    mergeGroup_ = 0;
    savedCursor_ = saved ? std::optional<size_t>(0) : std::nullopt;
    open_ = true;
    ++revision_;
}
bool GameplayCameraEditorDocument::New(std::string_view preset, std::string &message, bool discard) {
    if (open_ && IsDirty() && !discard) {
        message = "Save or explicitly discard the current camera draft first.";
        return false;
    }
    profile_ = CAMERA::MakeGameplayCameraPreset(preset);
    guid_ = {};
    path_.clear();
    ResetHistory(false);
    message.clear();
    return true;
}
bool GameplayCameraEditorDocument::Open(const AssetDatabase &database, const AssetGuid &guid, std::string &message,
                                        bool discard) {
    if (open_ && guid == guid_ && !discard)
        return true;
    if (open_ && IsDirty() && !discard) {
        message = "Save or explicitly discard the current camera draft first.";
        return false;
    }
    const auto *record = database.FindByGuid(guid);
    if (!record || record->type != AssetType::GameplayCamera) {
        message = "Gameplay camera asset not found.";
        return false;
    }
    const auto path =
        record->sourcePath.is_absolute() ? record->sourcePath : database.GetProjectRoot() / record->sourcePath;
    CAMERA::GameplayCameraProfile profile;
    if (!LoadGameplayCameraAsset(path, profile, message))
        return false;
    profile_ = std::move(profile);
    guid_ = guid;
    path_ = path;
    sourceVersion_.Accept(path_);
    ResetHistory(true);
    return true;
}
bool GameplayCameraEditorDocument::Save(AssetDatabase &database, std::string &message, bool saveAs) {
    if (!open_) {
        message = "No camera document is open.";
        return false;
    }
    if (!saveAs && guid_.IsValid()) {
        const auto *record = database.FindByGuid(guid_);
        if (!record || std::filesystem::weakly_canonical(database.GetProjectRoot() / record->sourcePath) !=
                           std::filesystem::weakly_canonical(path_)) {
            message = "This camera draft belongs to another project. Use Save As to create a copy.";
            return false;
        }
        if (!sourceVersion_.CanSave(message))
            return false;
    }
    auto path = path_;
    const bool create = saveAs || !guid_.IsValid();
    if (create) {
        const auto directory = database.GetAssetsRoot() / "Cameras";
        path = directory / "GameplayCamera.hcamera";
        for (unsigned suffix = 2; std::filesystem::exists(path); ++suffix)
            path = directory / ("GameplayCamera_" + std::to_string(suffix) + ".hcamera");
    }
    if (!SaveGameplayCameraAsset(path, profile_, message))
        return false;
    if (!database.ScanAssets(create)) {
        message = "Camera saved, but asset scan failed.";
        return false;
    }
    auto *record = database.FindByPath(path);
    if (!record) {
        message = "Camera saved, but asset metadata is missing.";
        return false;
    }
    record->displayName = profile_.name;
    record->meta.displayName = profile_.name;
    if (!database.WriteMeta(*record)) {
        message = "Camera saved, but metadata update failed.";
        return false;
    }
    const auto guid = record->guid;
    if (!database.ImportAsset(guid)) {
        message = "Camera saved, but asset validation failed.";
        return false;
    }
    guid_ = guid;
    path_ = path;
    sourceVersion_.Accept(path_);
    savedCursor_ = cursor_;
    SealMerge();
    message.clear();
    return true;
}
bool GameplayCameraEditorDocument::Apply(CAMERA::GameplayCameraProfile profile, std::string &message,
                                         uint64_t mergeGroup) {
    if (!CAMERA::ValidateGameplayCameraProfile(profile, message))
        return false;
    if (CAMERA::WriteGameplayCameraProfile(profile) == CAMERA::WriteGameplayCameraProfile(profile_))
        return true;
    if (savedCursor_ && *savedCursor_ > cursor_)
        savedCursor_.reset();
    history_.resize(cursor_);
    if (mergeGroup && mergeGroup == mergeGroup_ && cursor_ && (!savedCursor_ || *savedCursor_ != cursor_))
        history_.back().after = profile;
    else {
        history_.push_back({profile_, profile});
        ++cursor_;
    }
    profile_ = std::move(profile);
    mergeGroup_ = mergeGroup;
    ++revision_;
    open_ = true;
    return true;
}
bool GameplayCameraEditorDocument::Undo() {
    if (!CanUndo())
        return false;
    profile_ = history_[--cursor_].before;
    SealMerge();
    ++revision_;
    return true;
}
bool GameplayCameraEditorDocument::Redo() {
    if (!CanRedo())
        return false;
    profile_ = history_[cursor_++].after;
    SealMerge();
    ++revision_;
    return true;
}
} // namespace HIKARI::EDITOR
