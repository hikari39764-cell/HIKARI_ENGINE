#include "Editor/Documents/HIKARI_DialogueEditorDocument.h"

#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Authoring/HIKARI_DialogueAssetAuthoringService.h"

namespace HIKARI::EDITOR {

    void DialogueEditorDocument::New(std::string name) {
        definition_ = {};
        definition_.name = name.empty() ? "New Dialogue" : std::move(name);
        definition_.entryNodeId = "entry";
        definition_.fallbackLocale = "en-US";
        DIALOGUE::DialogueNode entry{};
        entry.id = "entry";
        entry.type = DIALOGUE::DialogueNodeType::Line;
        entry.speaker.values["en-US"] = "Narrator";
        entry.speaker.values["ja-JP"] = "ナレーター";
        entry.text.values["en-US"] = "New dialogue";
        entry.text.values["ja-JP"] = "新しい会話";
        entry.nextNodeId = "end";
        DIALOGUE::DialogueNode end{};
        end.id = "end";
        end.type = DIALOGUE::DialogueNodeType::End;
        definition_.nodes = { std::move(entry), std::move(end) };
        DIALOGUE::NormalizeDialogue(definition_);
        assetGuid_ = {};
        open_ = true;
        ResetHistory(false);
    }

    bool DialogueEditorDocument::Open(
        const AssetDatabase& database,
        const AssetGuid& guid,
        std::string& outMessage) {
        DIALOGUE::DialogueDefinition loaded{};
        if (!LoadDialogueForAuthoring(
                database, guid, loaded, outMessage)) {
            return false;
        }
        definition_ = std::move(loaded);
        assetGuid_ = guid;
        open_ = true;
        ResetHistory(true);
        return true;
    }

    bool DialogueEditorDocument::Save(
        AssetDatabase& database,
        std::string& outMessage) {
        DIALOGUE::NormalizeDialogue(definition_);
        const DialogueAssetAuthoringResult result = assetGuid_.IsValid()
            ? UpdateDialogueAsset(database, assetGuid_, definition_)
            : CreateDialogueAsset(database, definition_);
        outMessage = result.message;
        if (!result.success) return false;
        assetGuid_ = result.assetGuid;
        savedHistoryCursor_ = historyCursor_;
        return true;
    }

    bool DialogueEditorDocument::Revert(
        const AssetDatabase& database,
        std::string& outMessage) {
        return assetGuid_.IsValid()
            ? Open(database, assetGuid_, outMessage)
            : (outMessage = "Save this dialogue before reverting", false);
    }

    DIALOGUE::DialogueDefinition&
        DialogueEditorDocument::Definition() noexcept {
        return definition_;
    }

    const DIALOGUE::DialogueDefinition&
        DialogueEditorDocument::Definition() const noexcept {
        return definition_;
    }

    const AssetGuid& DialogueEditorDocument::GetAssetGuid() const noexcept {
        return assetGuid_;
    }

    bool DialogueEditorDocument::IsOpen() const noexcept { return open_; }

    bool DialogueEditorDocument::IsDirty() const noexcept {
        return open_ && (!savedHistoryCursor_ ||
            *savedHistoryCursor_ != historyCursor_);
    }

    bool DialogueEditorDocument::CanUndo() const noexcept {
        return open_ && historyCursor_ > 0u;
    }

    bool DialogueEditorDocument::CanRedo() const noexcept {
        return open_ && historyCursor_ < history_.size();
    }

    void DialogueEditorDocument::RecordApplied(
        DIALOGUE::DialogueDefinition before,
        uint64_t mergeGroup) {
        if (historyCursor_ < history_.size()) {
            if (savedHistoryCursor_ &&
                *savedHistoryCursor_ > historyCursor_) {
                savedHistoryCursor_.reset();
            }
            history_.erase(history_.begin() + historyCursor_, history_.end());
        }
        const bool merge = mergeGroup != 0u && historyCursor_ > 0u &&
            historyCursor_ == history_.size() &&
            openMergeGroup_ == mergeGroup &&
            (!savedHistoryCursor_ ||
                *savedHistoryCursor_ != historyCursor_);
        if (merge) {
            history_.back().after = definition_;
            return;
        }
        history_.push_back({ std::move(before), definition_, mergeGroup });
        historyCursor_ = history_.size();
        openMergeGroup_ = mergeGroup;
    }

    void DialogueEditorDocument::SealMerge() noexcept {
        openMergeGroup_ = 0u;
    }

    bool DialogueEditorDocument::Undo() {
        if (!CanUndo()) return false;
        --historyCursor_;
        definition_ = history_[historyCursor_].before;
        openMergeGroup_ = 0u;
        return true;
    }

    bool DialogueEditorDocument::Redo() {
        if (!CanRedo()) return false;
        definition_ = history_[historyCursor_].after;
        ++historyCursor_;
        openMergeGroup_ = 0u;
        return true;
    }

    void DialogueEditorDocument::ResetHistory(bool saved) noexcept {
        history_.clear();
        historyCursor_ = 0u;
        savedHistoryCursor_ = saved
            ? std::optional<size_t>{ 0u }
            : std::nullopt;
        openMergeGroup_ = 0u;
    }

} // namespace HIKARI::EDITOR
