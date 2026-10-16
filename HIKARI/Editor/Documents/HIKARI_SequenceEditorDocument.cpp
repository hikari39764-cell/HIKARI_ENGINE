#include "Editor/Documents/HIKARI_SequenceEditorDocument.h"
#include "Scene/Sequencer/Serialization/HIKARI_CinematicSequenceJson.h"

#include <algorithm>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Authoring/HIKARI_StructuredAssetAuthoringService.h"

namespace HIKARI::EDITOR {

void SequenceEditorDocument::OpenEmbeddedScene() noexcept {
    sourceKind_ = SequenceEditorSourceKind::EmbeddedScene;
    assetGuid_ = {};
    sourcePath_.clear();
    displayName_ = "Scene Cinematics";
    settings_ = {};
    ResetHistory(true);
}

void SequenceEditorDocument::NewAsset(std::string name) {
    CinematicSequence sequence{};
    sequence.name = name.empty() ? "New Sequence" : std::move(name);
    SetAssetSettings(std::move(sequence));
    sourceKind_ = SequenceEditorSourceKind::TransientAsset;
    assetGuid_ = {};
    sourcePath_.clear();
    displayName_ = settings_.sequences.front().name;
    ResetHistory(false);
}

bool SequenceEditorDocument::OpenAsset(const AssetDatabase& assetDatabase, const AssetGuid& assetGuid,
                                       std::string& outMessage) {
    CinematicSequence sequence{};
    if (!LoadSequenceForAuthoring(assetDatabase, assetGuid, sequence, outMessage)) {
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
    sourceVersion_.Accept(assetDatabase, assetGuid_);
    sourcePath_ = record->sourcePath;
    displayName_ = record->displayName.empty() ? sourcePath_.stem().string() : record->displayName;
    ResetHistory(true);
    outMessage = "Opened Sequence asset: " + displayName_;
    return true;
}

bool SequenceEditorDocument::Save(AssetDatabase& assetDatabase, std::string& outMessage) {
    const CinematicSequence* sequence = GetAssetSequence();
    if (sequence == nullptr) {
        outMessage = "The embedded scene is saved with the scene document";
        return false;
    }
    if (sourceKind_ == SequenceEditorSourceKind::TransientAsset) {
        return SaveAs(assetDatabase, *sequence, outMessage);
    }
    if (assetGuid_.IsValid() && !sourceVersion_.CanSave(outMessage))
        return false;
    const StructuredAssetAuthoringResult result = UpdateSequenceAsset(assetDatabase, assetGuid_, *sequence);
    outMessage = result.message;
    if (!result.success) {
        return false;
    }
    sourceVersion_.Accept(assetDatabase, assetGuid_);
    history_.MarkSaved();
    displayName_ = sequence->name;
    return true;
}

bool SequenceEditorDocument::SaveAs(AssetDatabase& assetDatabase, const CinematicSequence& sequence,
                                    std::string& outMessage) {
    const StructuredAssetAuthoringResult result = CreateSequenceAsset(assetDatabase, sequence);
    outMessage = result.message;
    if (!result.success || !result.assetGuid.IsValid()) {
        return false;
    }
    assetGuid_ = result.assetGuid;
    sourcePath_ = result.sourcePath;
    sourceKind_ = SequenceEditorSourceKind::Asset;
    displayName_ = sequence.name;
    sourceVersion_.Accept(assetDatabase, assetGuid_);
    // 別名保存は別の資産。旧文書の関連草稿を Undo で新資産へ持ち込まない。
    ResetHistory(true);
    return true;
}

bool SequenceEditorDocument::Revert(const AssetDatabase& assetDatabase, std::string& outMessage) {
    if (sourceKind_ != SequenceEditorSourceKind::Asset || !assetGuid_.IsValid()) {
        outMessage = "Only a saved Sequence asset can be reverted";
        return false;
    }
    return OpenAsset(assetDatabase, assetGuid_, outMessage);
}

bool SequenceEditorDocument::RefreshCleanSource(const AssetDatabase& database, std::string& message) {
    if (IsDirty() || sourceKind_ != SequenceEditorSourceKind::Asset)
        return true;
    if (sourceVersion_.CanSave(message))
        return true;
    return Revert(database, message);
}

SceneCinematicsSettings& SequenceEditorDocument::GetSettings(SceneCinematicsSettings& embeddedSettings) noexcept {
    return IsEmbeddedScene() ? embeddedSettings : settings_;
}

const SceneCinematicsSettings& SequenceEditorDocument::GetSettings(
    const SceneCinematicsSettings& embeddedSettings) const noexcept {
    return IsEmbeddedScene() ? embeddedSettings : settings_;
}

CinematicSequence* SequenceEditorDocument::GetAssetSequence() noexcept {
    return IsEmbeddedScene() || settings_.sequences.empty() ? nullptr : &settings_.sequences.front();
}

const CinematicSequence* SequenceEditorDocument::GetAssetSequence() const noexcept {
    return IsEmbeddedScene() || settings_.sequences.empty() ? nullptr : &settings_.sequences.front();
}

void SequenceEditorDocument::RecordApplied(SceneCinematicsSettings before, uint64_t mergeGroup) {
    if (!IsEmbeddedScene()) {
        // 同じドラッグが出発点へ戻った時は空の Undo と dirty を残さない。
        const auto equivalent = [](const auto& a, const auto& b) {
            nlohmann::json left, right;
            SerializeSceneCinematicsJson(a, left);
            SerializeSceneCinematicsJson(b, right);
            return left == right;
        };
        if (!equivalent(before, settings_))
            history_.Record(std::move(before), settings_, mergeGroup, {}, equivalent);
    }
}

void SequenceEditorDocument::SealMerge() noexcept { history_.SealMerge(); }

bool SequenceEditorDocument::Undo() { return !IsEmbeddedScene() && history_.Undo(settings_); }

bool SequenceEditorDocument::Redo() { return !IsEmbeddedScene() && history_.Redo(settings_); }

bool SequenceEditorDocument::CanUndo() const noexcept { return !IsEmbeddedScene() && history_.CanUndo(); }

bool SequenceEditorDocument::CanRedo() const noexcept { return !IsEmbeddedScene() && history_.CanRedo(); }

bool SequenceEditorDocument::IsDirty() const noexcept { return !IsEmbeddedScene() && history_.IsDirty(); }

SequenceEditorSourceKind SequenceEditorDocument::GetSourceKind() const noexcept { return sourceKind_; }

bool SequenceEditorDocument::IsEmbeddedScene() const noexcept {
    return sourceKind_ == SequenceEditorSourceKind::EmbeddedScene;
}

bool SequenceEditorDocument::IsExternalDocument() const noexcept { return !IsEmbeddedScene(); }

const AssetGuid& SequenceEditorDocument::GetAssetGuid() const noexcept { return assetGuid_; }

const std::filesystem::path& SequenceEditorDocument::GetSourcePath() const noexcept { return sourcePath_; }

const std::string& SequenceEditorDocument::GetDisplayName() const noexcept { return displayName_; }

void SequenceEditorDocument::ResetHistory(bool saved) noexcept { history_.Reset(saved); }

void SequenceEditorDocument::SetAssetSettings(CinematicSequence sequence) {
    NormalizeCinematicSequence(sequence);
    settings_ = {};
    settings_.defaultSequenceId = sequence.id;
    settings_.sequences.clear();
    settings_.sequences.push_back(std::move(sequence));
    NormalizeSceneCinematicsSettings(settings_);
}

} // namespace HIKARI::EDITOR
