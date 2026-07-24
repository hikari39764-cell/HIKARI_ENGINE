#include "Editor/Documents/HIKARI_AnimationStateMachineEditorDocument.h"

#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Authoring/HIKARI_AnimationStateMachineAssetAuthoringService.h"

namespace HIKARI::EDITOR {

    void AnimationStateMachineEditorDocument::New(std::string name) {
        definition_ = {};
        definition_.name = name.empty()
            ? "New Animation State Machine"
            : std::move(name);
        ANIMATION::NormalizeAnimationStateMachine(definition_);
        assetGuid_ = {};
        open_ = true;
        ResetHistory(false);
    }

    bool AnimationStateMachineEditorDocument::Open(
        const AssetDatabase& database,
        const AssetGuid& guid,
        std::string& outMessage) {
        ANIMATION::AnimationStateMachineDefinition loaded{};
        if (!LoadAnimationStateMachineForAuthoring(
                database, guid, loaded, outMessage)) {
            return false;
        }
        definition_ = std::move(loaded);
        assetGuid_ = guid;
        open_ = true;
        ResetHistory(true);
        return true;
    }

    bool AnimationStateMachineEditorDocument::Save(
        AssetDatabase& database,
        std::string& outMessage) {
        ANIMATION::NormalizeAnimationStateMachine(definition_);
        const AnimationStateMachineAssetAuthoringResult result =
            assetGuid_.IsValid()
            ? UpdateAnimationStateMachineAsset(
                database, assetGuid_, definition_)
            : CreateAnimationStateMachineAsset(database, definition_);
        outMessage = result.message;
        if (!result.success) return false;
        assetGuid_ = result.assetGuid;
        savedHistoryCursor_ = historyCursor_;
        return true;
    }

    bool AnimationStateMachineEditorDocument::Revert(
        const AssetDatabase& database,
        std::string& outMessage) {
        return assetGuid_.IsValid()
            ? Open(database, assetGuid_, outMessage)
            : (outMessage = "Save this asset before reverting", false);
    }

    ANIMATION::AnimationStateMachineDefinition&
        AnimationStateMachineEditorDocument::Definition() noexcept {
        return definition_;
    }

    const ANIMATION::AnimationStateMachineDefinition&
        AnimationStateMachineEditorDocument::Definition() const noexcept {
        return definition_;
    }

    const AssetGuid& AnimationStateMachineEditorDocument::GetAssetGuid()
        const noexcept {
        return assetGuid_;
    }

    bool AnimationStateMachineEditorDocument::IsOpen() const noexcept {
        return open_;
    }

    bool AnimationStateMachineEditorDocument::IsDirty() const noexcept {
        return open_ && (!savedHistoryCursor_ ||
            *savedHistoryCursor_ != historyCursor_);
    }

    bool AnimationStateMachineEditorDocument::CanUndo() const noexcept {
        return open_ && historyCursor_ > 0u;
    }

    bool AnimationStateMachineEditorDocument::CanRedo() const noexcept {
        return open_ && historyCursor_ < history_.size();
    }

    void AnimationStateMachineEditorDocument::RecordApplied(
        ANIMATION::AnimationStateMachineDefinition before,
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

    void AnimationStateMachineEditorDocument::SealMerge() noexcept {
        openMergeGroup_ = 0u;
    }

    bool AnimationStateMachineEditorDocument::Undo() {
        if (!CanUndo()) return false;
        --historyCursor_;
        definition_ = history_[historyCursor_].before;
        openMergeGroup_ = 0u;
        return true;
    }

    bool AnimationStateMachineEditorDocument::Redo() {
        if (!CanRedo()) return false;
        definition_ = history_[historyCursor_].after;
        ++historyCursor_;
        openMergeGroup_ = 0u;
        return true;
    }

    void AnimationStateMachineEditorDocument::ResetHistory(
        bool saved) noexcept {
        history_.clear();
        historyCursor_ = 0u;
        savedHistoryCursor_ = saved
            ? std::optional<size_t>{ 0u }
            : std::nullopt;
        openMergeGroup_ = 0u;
    }

} // namespace HIKARI::EDITOR
