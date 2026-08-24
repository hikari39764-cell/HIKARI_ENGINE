#include "Editor/Documents/HIKARI_CombatActionEditorDocument.h"

#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Authoring/HIKARI_CombatActionAssetAuthoringService.h"

namespace HIKARI::EDITOR {

    void CombatActionEditorDocument::New(std::string name) {
        definition_ = {};
        definition_.displayName = name.empty()
            ? "New Combat Action" : std::move(name);
        definition_.actionId = "Action.New";
        definition_.durationSeconds = 1.0f;
        definition_.displayFramesPerSecond = 30u;
        definition_.animationSegments.push_back({
            1u, "Idle", 0.0f, 1.0f, 0.0f, 1.0f, 0.08f, 0.08f, false
        });
        definition_.phases = {
            { 2u, ACTION::CombatActionPhaseKind::Startup, 0.0f, 0.2f },
            { 3u, ACTION::CombatActionPhaseKind::Active, 0.2f, 0.4f },
            { 4u, ACTION::CombatActionPhaseKind::Recovery, 0.4f, 1.0f },
        };
        ACTION::NormalizeCombatAction(definition_);
        assetGuid_ = {};
        open_ = true;
        ResetHistory(false);
    }

    bool CombatActionEditorDocument::Open(
        const AssetDatabase& database,
        const AssetGuid& guid,
        std::string& outMessage) {
        ACTION::CombatActionDefinition loaded{};
        if (!LoadCombatActionForAuthoring(
                database, guid, loaded, outMessage)) {
            return false;
        }
        definition_ = std::move(loaded);
        assetGuid_ = guid;
        open_ = true;
        ResetHistory(true);
        return true;
    }

    bool CombatActionEditorDocument::Save(
        AssetDatabase& database,
        std::string& outMessage) {
        ACTION::NormalizeCombatAction(definition_);
        const CombatActionAssetAuthoringResult result = assetGuid_.IsValid()
            ? UpdateCombatActionAsset(database, assetGuid_, definition_)
            : CreateCombatActionAsset(database, definition_);
        outMessage = result.message;
        if (!result.success) return false;
        assetGuid_ = result.assetGuid;
        savedHistoryCursor_ = historyCursor_;
        return true;
    }

    bool CombatActionEditorDocument::Revert(
        const AssetDatabase& database,
        std::string& outMessage) {
        return assetGuid_.IsValid()
            ? Open(database, assetGuid_, outMessage)
            : (outMessage = "Save this Combat Action before reverting", false);
    }

    ACTION::CombatActionDefinition&
        CombatActionEditorDocument::Definition() noexcept {
        return definition_;
    }

    const ACTION::CombatActionDefinition&
        CombatActionEditorDocument::Definition() const noexcept {
        return definition_;
    }

    const AssetGuid&
        CombatActionEditorDocument::GetAssetGuid() const noexcept {
        return assetGuid_;
    }

    bool CombatActionEditorDocument::IsOpen() const noexcept { return open_; }

    bool CombatActionEditorDocument::IsDirty() const noexcept {
        return open_ && (!savedHistoryCursor_ ||
            *savedHistoryCursor_ != historyCursor_);
    }

    bool CombatActionEditorDocument::CanUndo() const noexcept {
        return open_ && historyCursor_ > 0u;
    }

    bool CombatActionEditorDocument::CanRedo() const noexcept {
        return open_ && historyCursor_ < history_.size();
    }

    void CombatActionEditorDocument::RecordApplied(
        ACTION::CombatActionDefinition before,
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

    void CombatActionEditorDocument::SealMerge() noexcept {
        openMergeGroup_ = 0u;
    }

    bool CombatActionEditorDocument::Undo() {
        if (!CanUndo()) return false;
        --historyCursor_;
        definition_ = history_[historyCursor_].before;
        openMergeGroup_ = 0u;
        return true;
    }

    bool CombatActionEditorDocument::Redo() {
        if (!CanRedo()) return false;
        definition_ = history_[historyCursor_].after;
        ++historyCursor_;
        openMergeGroup_ = 0u;
        return true;
    }

    void CombatActionEditorDocument::ResetHistory(bool saved) noexcept {
        history_.clear();
        historyCursor_ = 0u;
        savedHistoryCursor_ = saved
            ? std::optional<size_t>{ 0u }
            : std::nullopt;
        openMergeGroup_ = 0u;
    }

} // namespace HIKARI::EDITOR
