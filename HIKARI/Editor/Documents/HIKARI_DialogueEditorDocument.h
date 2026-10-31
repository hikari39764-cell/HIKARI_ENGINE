#pragma once
#include "Editor/History/HIKARI_DocumentSnapshotHistory.h"
#include "Editor/Documents/HIKARI_EditorSourceVersion.h"

#include <string>

#include "Core/HIKARI_AssetGuid.h"
#include "Gameplay/Dialogue/HIKARI_DialogueDefinition.h"

namespace HIKARI {
class AssetDatabase;
}

namespace HIKARI::EDITOR {

class DialogueEditorDocument {
public:
    void New(std::string name = "New Dialogue");
    bool Open(const AssetDatabase& database, const AssetGuid& guid, std::string& outMessage);
    bool Save(AssetDatabase& database, std::string& outMessage);
    bool Revert(const AssetDatabase& database, std::string& outMessage);
    void Close() noexcept;

    DIALOGUE::DialogueDefinition& Definition() noexcept;
    const DIALOGUE::DialogueDefinition& Definition() const noexcept;
    const AssetGuid& GetAssetGuid() const noexcept;
    bool IsOpen() const noexcept;
    bool IsDirty() const noexcept;
    bool CanUndo() const noexcept;
    bool CanRedo() const noexcept;
    void RecordApplied(DIALOGUE::DialogueDefinition before, uint64_t mergeGroup = 0u);
    void SealMerge() noexcept;
    bool Undo();
    bool Redo();

private:
    EditorSourceVersion sourceVersion_;

    void ResetHistory(bool saved) noexcept;

    AssetGuid assetGuid_{};
    DIALOGUE::DialogueDefinition definition_{};
    DocumentSnapshotHistory<DIALOGUE::DialogueDefinition> history_;
    bool open_ = false;
};

} // namespace HIKARI::EDITOR
