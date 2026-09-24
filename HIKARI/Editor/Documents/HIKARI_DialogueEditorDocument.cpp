#include "Editor/Documents/HIKARI_DialogueEditorDocument.h"

#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Authoring/HIKARI_StructuredAssetAuthoringService.h"

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
    definition_.nodes = {std::move(entry), std::move(end)};
    DIALOGUE::NormalizeDialogue(definition_);
    assetGuid_ = {};
    open_ = true;
    ResetHistory(false);
}

bool DialogueEditorDocument::Open(const AssetDatabase& database, const AssetGuid& guid, std::string& outMessage) {
    DIALOGUE::DialogueDefinition loaded{};
    if (!LoadDialogueForAuthoring(database, guid, loaded, outMessage)) {
        return false;
    }
    definition_ = std::move(loaded);
    assetGuid_ = guid;
    sourceVersion_.Accept(database, guid);
    open_ = true;
    ResetHistory(true);
    return true;
}

bool DialogueEditorDocument::Save(AssetDatabase& database, std::string& outMessage) {
    if (assetGuid_.IsValid() && !sourceVersion_.CanSave(outMessage))
        return false;
    DIALOGUE::NormalizeDialogue(definition_);
    const StructuredAssetAuthoringResult result = assetGuid_.IsValid()
                                                      ? UpdateDialogueAsset(database, assetGuid_, definition_)
                                                      : CreateDialogueAsset(database, definition_);
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

bool DialogueEditorDocument::Revert(const AssetDatabase& database, std::string& outMessage) {
    return assetGuid_.IsValid() ? Open(database, assetGuid_, outMessage)
                                : (outMessage = "Save this dialogue before reverting", false);
}

void DialogueEditorDocument::Close() noexcept {
    assetGuid_ = {};
    definition_ = {};
    history_.Reset(false);
    open_ = false;
}

DIALOGUE::DialogueDefinition& DialogueEditorDocument::Definition() noexcept { return definition_; }

const DIALOGUE::DialogueDefinition& DialogueEditorDocument::Definition() const noexcept { return definition_; }

const AssetGuid& DialogueEditorDocument::GetAssetGuid() const noexcept { return assetGuid_; }

bool DialogueEditorDocument::IsOpen() const noexcept { return open_; }

bool DialogueEditorDocument::IsDirty() const noexcept { return open_ && history_.IsDirty(); }

bool DialogueEditorDocument::CanUndo() const noexcept { return open_ && history_.CanUndo(); }

bool DialogueEditorDocument::CanRedo() const noexcept { return open_ && history_.CanRedo(); }

void DialogueEditorDocument::RecordApplied(DIALOGUE::DialogueDefinition before, uint64_t mergeGroup) {
    history_.Record(std::move(before), definition_, mergeGroup);
}

void DialogueEditorDocument::SealMerge() noexcept { history_.SealMerge(); }

bool DialogueEditorDocument::Undo() { return open_ && history_.Undo(definition_); }

bool DialogueEditorDocument::Redo() { return open_ && history_.Redo(definition_); }

void DialogueEditorDocument::ResetHistory(bool saved) noexcept { history_.Reset(saved); }

} // namespace HIKARI::EDITOR
