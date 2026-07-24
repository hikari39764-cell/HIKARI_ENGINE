#include "Editor/Documents/HIKARI_SequenceEditorDocument.h"

#include <algorithm>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Authoring/HIKARI_SequenceAssetAuthoringService.h"

namespace HIKARI::EDITOR {

    void SequenceEditorDocument::OpenEmbeddedScene() noexcept {
        sourceKind_ = SequenceEditorSourceKind::EmbeddedScene;
        assetGuid_ = {};
        sourcePath_.clear();
        displayName_ = "Scene Cinematics";
        assetSettings_ = {};
        ResetHistory(true);
    }

    void SequenceEditorDocument::NewAsset(std::string name) {
        CinematicSequence sequence{};
        sequence.name = name.empty() ? "New Sequence" : std::move(name);
        SetAssetSettings(std::move(sequence));
        sourceKind_ = SequenceEditorSourceKind::TransientAsset;
        assetGuid_ = {};
        sourcePath_.clear();
        displayName_ = assetSettings_.sequences.front().name;
        ResetHistory(false);
    }

    bool SequenceEditorDocument::OpenAsset(
        const AssetDatabase& assetDatabase,
        const AssetGuid& assetGuid,
        std::string& outMessage) {

        CinematicSequence sequence{};
        if (!LoadSequenceForAuthoring(
                assetDatabase,
                assetGuid,
                sequence,
                outMessage)) {
            return false;
        }
        const AssetRecord* record = assetDatabase.FindByGuid(assetGuid);
        if (record == nullptr || record->type != AssetType::Sequence) {
            outMessage = "Sequence asset record disappeared while opening";
            return false;
        }
        SetAssetSettings(std::move(sequence));
        sourceKind_ = SequenceEditorSourceKind::Asset;
        assetGuid_ = record->guid;
        sourcePath_ = record->sourcePath;
        displayName_ = record->displayName.empty()
            ? sourcePath_.stem().string()
            : record->displayName;
        ResetHistory(true);
        outMessage = "Opened Sequence asset: " + displayName_;
        return true;
    }

    bool SequenceEditorDocument::Save(
        AssetDatabase& assetDatabase,
        std::string& outMessage) {

        const CinematicSequence* sequence = GetAssetSequence();
        if (sequence == nullptr) {
            outMessage = "The embedded scene is saved with the scene document";
            return false;
        }
        if (sourceKind_ == SequenceEditorSourceKind::TransientAsset) {
            return SaveAs(assetDatabase, *sequence, outMessage);
        }
        const SequenceAssetAuthoringResult result = UpdateSequenceAsset(
            assetDatabase,
            assetGuid_,
            *sequence);
        outMessage = result.message;
        if (!result.success) {
            return false;
        }
        savedHistoryCursor_ = historyCursor_;
        displayName_ = sequence->name;
        return true;
    }

    bool SequenceEditorDocument::SaveAs(
        AssetDatabase& assetDatabase,
        const CinematicSequence& sequence,
        std::string& outMessage) {

        const SequenceAssetAuthoringResult result = CreateSequenceAsset(
            assetDatabase,
            sequence);
        outMessage = result.message;
        if (!result.success || !result.assetGuid.IsValid()) {
            return false;
        }
        return OpenAsset(assetDatabase, result.assetGuid, outMessage);
    }

    bool SequenceEditorDocument::Revert(
        const AssetDatabase& assetDatabase,
        std::string& outMessage) {

        if (sourceKind_ != SequenceEditorSourceKind::Asset ||
            !assetGuid_.IsValid()) {
            outMessage = "Only a saved Sequence asset can be reverted";
            return false;
        }
        return OpenAsset(assetDatabase, assetGuid_, outMessage);
    }

    SceneCinematicsSettings& SequenceEditorDocument::GetSettings(
        SceneCinematicsSettings& embeddedSettings) noexcept {

        return IsEmbeddedScene() ? embeddedSettings : assetSettings_;
    }

    const SceneCinematicsSettings& SequenceEditorDocument::GetSettings(
        const SceneCinematicsSettings& embeddedSettings) const noexcept {

        return IsEmbeddedScene() ? embeddedSettings : assetSettings_;
    }

    CinematicSequence* SequenceEditorDocument::GetAssetSequence() noexcept {
        return IsEmbeddedScene() || assetSettings_.sequences.empty()
            ? nullptr
            : &assetSettings_.sequences.front();
    }

    const CinematicSequence*
        SequenceEditorDocument::GetAssetSequence() const noexcept {

        return IsEmbeddedScene() || assetSettings_.sequences.empty()
            ? nullptr
            : &assetSettings_.sequences.front();
    }

    void SequenceEditorDocument::RecordApplied(
        SceneCinematicsSettings before,
        uint64_t mergeGroup) {

        if (IsEmbeddedScene()) {
            return;
        }
        if (historyCursor_ < history_.size()) {
            if (savedHistoryCursor_ &&
                *savedHistoryCursor_ > historyCursor_) {
                savedHistoryCursor_.reset();
            }
            history_.erase(
                history_.begin() + historyCursor_,
                history_.end());
        }
        const bool canMerge = mergeGroup != 0 &&
            historyCursor_ > 0 &&
            historyCursor_ == history_.size() &&
            openMergeGroup_ == mergeGroup &&
            (!savedHistoryCursor_ ||
                *savedHistoryCursor_ != historyCursor_);
        if (canMerge) {
            history_.back().after = assetSettings_;
            return;
        }
        history_.push_back({
            std::move(before),
            assetSettings_,
            mergeGroup
        });
        historyCursor_ = history_.size();
        openMergeGroup_ = mergeGroup;
    }

    void SequenceEditorDocument::SealMerge() noexcept {
        openMergeGroup_ = 0;
    }

    bool SequenceEditorDocument::Undo() {
        if (!CanUndo()) {
            return false;
        }
        --historyCursor_;
        assetSettings_ = history_[historyCursor_].before;
        openMergeGroup_ = 0;
        return true;
    }

    bool SequenceEditorDocument::Redo() {
        if (!CanRedo()) {
            return false;
        }
        assetSettings_ = history_[historyCursor_].after;
        ++historyCursor_;
        openMergeGroup_ = 0;
        return true;
    }

    bool SequenceEditorDocument::CanUndo() const noexcept {
        return !IsEmbeddedScene() && historyCursor_ > 0;
    }

    bool SequenceEditorDocument::CanRedo() const noexcept {
        return !IsEmbeddedScene() && historyCursor_ < history_.size();
    }

    bool SequenceEditorDocument::IsDirty() const noexcept {
        if (IsEmbeddedScene()) {
            return false;
        }
        return !savedHistoryCursor_ ||
            *savedHistoryCursor_ != historyCursor_;
    }

    SequenceEditorSourceKind
        SequenceEditorDocument::GetSourceKind() const noexcept {

        return sourceKind_;
    }

    bool SequenceEditorDocument::IsEmbeddedScene() const noexcept {
        return sourceKind_ == SequenceEditorSourceKind::EmbeddedScene;
    }

    bool SequenceEditorDocument::IsExternalDocument() const noexcept {
        return !IsEmbeddedScene();
    }

    const AssetGuid& SequenceEditorDocument::GetAssetGuid() const noexcept {
        return assetGuid_;
    }

    const std::filesystem::path&
        SequenceEditorDocument::GetSourcePath() const noexcept {

        return sourcePath_;
    }

    const std::string&
        SequenceEditorDocument::GetDisplayName() const noexcept {

        return displayName_;
    }

    void SequenceEditorDocument::ResetHistory(bool saved) noexcept {
        history_.clear();
        historyCursor_ = 0;
        savedHistoryCursor_ = saved
            ? std::optional<size_t>{ 0 }
            : std::nullopt;
        openMergeGroup_ = 0;
    }

    void SequenceEditorDocument::SetAssetSettings(
        CinematicSequence sequence) {

        NormalizeCinematicSequence(sequence);
        assetSettings_ = {};
        assetSettings_.defaultSequenceId = sequence.id;
        assetSettings_.sequences.clear();
        assetSettings_.sequences.push_back(std::move(sequence));
        NormalizeSceneCinematicsSettings(assetSettings_);
    }

} // namespace HIKARI::EDITOR
