#pragma once
#include "Editor/History/HIKARI_DocumentSnapshotHistory.h"

#include <filesystem>

#include "Gameplay/Dialogue/HIKARI_DialogueProjectStyle.h"

namespace HIKARI {
class AssetDatabase;
}

namespace HIKARI::EDITOR {

class DialogueStyleEditorDocument final {
public:
    void New();
    bool Open(const std::filesystem::path& projectRoot, const AssetDatabase& database, std::string& outMessage);
    bool Save(const std::filesystem::path& projectRoot, const AssetDatabase& database, std::string& outMessage);
    void DiscardChanges(const std::filesystem::path& projectRoot, const AssetDatabase& database);

    DIALOGUE::DialogueProjectStyle& Style() noexcept;
    const DIALOGUE::DialogueProjectStyle& Style() const noexcept;
    bool IsOpen() const noexcept;
    bool IsDirty() const noexcept;
    bool CanUndo() const noexcept;
    bool CanRedo() const noexcept;
    void RecordApplied(DIALOGUE::DialogueProjectStyle before, uint64_t mergeGroup = 0u);
    void SealMerge() noexcept;
    bool Undo();
    bool Redo();

private:
    void ResetHistory(bool saved) noexcept;

    DIALOGUE::DialogueProjectStyle style_{};
    DocumentSnapshotHistory<DIALOGUE::DialogueProjectStyle> history_;
    bool open_ = false;
};

} // namespace HIKARI::EDITOR
