#include "Editor/Documents/HIKARI_AnimationStateMachineEditorDocument.h"

#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Authoring/HIKARI_StructuredAssetAuthoringService.h"

namespace HIKARI::EDITOR {

void AnimationStateMachineEditorDocument::New(std::string name) {
    definition_ = {};
    definition_.name = name.empty() ? "New Animation State Machine" : std::move(name);
    ANIMATION::NormalizeAnimationStateMachine(definition_);
    assetGuid_ = {};
    open_ = true;
    ResetHistory(false);
}

bool AnimationStateMachineEditorDocument::Open(const AssetDatabase& database, const AssetGuid& guid,
                                               std::string& outMessage) {
    ANIMATION::AnimationStateMachineDefinition loaded{};
    if (!LoadAnimationStateMachineForAuthoring(database, guid, loaded, outMessage)) {
        return false;
    }
    definition_ = std::move(loaded);
    assetGuid_ = guid;
    sourceVersion_.Accept(database, guid);
    open_ = true;
    ResetHistory(true);
    return true;
}

bool AnimationStateMachineEditorDocument::Save(AssetDatabase& database, std::string& outMessage) {
    if (assetGuid_.IsValid() && !sourceVersion_.CanSave(outMessage))
        return false;
    ANIMATION::NormalizeAnimationStateMachine(definition_);
    const StructuredAssetAuthoringResult result =
        assetGuid_.IsValid() ? UpdateAnimationStateMachineAsset(database, assetGuid_, definition_)
                             : CreateAnimationStateMachineAsset(database, definition_);
    outMessage = result.message;
    if (result.assetGuid.IsValid()) {
        assetGuid_ = result.assetGuid;
        sourceVersion_.Accept(database, assetGuid_);
    }
    if (!result.success)
        return false;
    history_.MarkSaved();
    return true;
}

bool AnimationStateMachineEditorDocument::Revert(const AssetDatabase& database, std::string& outMessage) {
    return assetGuid_.IsValid() ? Open(database, assetGuid_, outMessage)
                                : (outMessage = "Save this asset before reverting", false);
}

ANIMATION::AnimationStateMachineDefinition& AnimationStateMachineEditorDocument::Definition() noexcept {
    return definition_;
}

const ANIMATION::AnimationStateMachineDefinition& AnimationStateMachineEditorDocument::Definition() const noexcept {
    return definition_;
}

const AssetGuid& AnimationStateMachineEditorDocument::GetAssetGuid() const noexcept { return assetGuid_; }

bool AnimationStateMachineEditorDocument::IsOpen() const noexcept { return open_; }

bool AnimationStateMachineEditorDocument::IsDirty() const noexcept { return open_ && history_.IsDirty(); }

bool AnimationStateMachineEditorDocument::CanUndo() const noexcept { return open_ && history_.CanUndo(); }

bool AnimationStateMachineEditorDocument::CanRedo() const noexcept { return open_ && history_.CanRedo(); }

void AnimationStateMachineEditorDocument::RecordApplied(ANIMATION::AnimationStateMachineDefinition before,
                                                        uint64_t mergeGroup) {
    history_.Record(std::move(before), definition_, mergeGroup);
}

void AnimationStateMachineEditorDocument::SealMerge() noexcept { history_.SealMerge(); }

bool AnimationStateMachineEditorDocument::Undo() { return open_ && history_.Undo(definition_); }

bool AnimationStateMachineEditorDocument::Redo() { return open_ && history_.Redo(definition_); }

void AnimationStateMachineEditorDocument::ResetHistory(bool saved) noexcept { history_.Reset(saved); }

} // namespace HIKARI::EDITOR
