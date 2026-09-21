#pragma once
#include "Assets/Camera/HIKARI_GameplayCameraAsset.h"
#include "Editor/Documents/HIKARI_EditorSourceVersion.h"
#include <optional>

namespace HIKARI::EDITOR {
class GameplayCameraEditorDocument {
  public:
    bool New(std::string_view preset, std::string &message, bool discard = false);
    bool Open(const AssetDatabase &database, const AssetGuid &guid, std::string &message, bool discard = false);
    bool Save(AssetDatabase &database, std::string &message, bool saveAs = false);
    bool Apply(CAMERA::GameplayCameraProfile profile, std::string &message, uint64_t mergeGroup = 0);
    void SealMerge() { mergeGroup_ = 0; }
    bool Undo();
    bool Redo();
    bool CanUndo() const { return cursor_ > 0; }
    bool CanRedo() const { return cursor_ < history_.size(); }
    bool IsDirty() const { return !savedCursor_ || *savedCursor_ != cursor_; }
    bool IsOpen() const { return open_; }
    const CAMERA::GameplayCameraProfile &Profile() const { return profile_; }
    const AssetGuid &Guid() const { return guid_; }
    uint64_t Revision() const { return revision_; }

  private:
    void ResetHistory(bool saved);
    struct Entry {
        CAMERA::GameplayCameraProfile before, after;
    };
    CAMERA::GameplayCameraProfile profile_;
    AssetGuid guid_;
    std::filesystem::path path_;
    EditorSourceVersion sourceVersion_;
    std::vector<Entry> history_;
    size_t cursor_ = 0;
    std::optional<size_t> savedCursor_ = 0;
    uint64_t revision_ = 1, mergeGroup_ = 0;
    bool open_ = false;
};
} // namespace HIKARI::EDITOR
