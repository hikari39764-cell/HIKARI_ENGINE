#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Gameplay/Dialogue/HIKARI_DialogueDefinition.h"

namespace HIKARI { class AssetDatabase; }

namespace HIKARI::EDITOR {

    class DialogueEditorDocument {
    public:
        void New(std::string name = "New Dialogue");
        bool Open(
            const AssetDatabase& database,
            const AssetGuid& guid,
            std::string& outMessage);
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
        void RecordApplied(
            DIALOGUE::DialogueDefinition before,
            uint64_t mergeGroup = 0u);
        void SealMerge() noexcept;
        bool Undo();
        bool Redo();

    private:
        struct HistoryEntry {
            DIALOGUE::DialogueDefinition before{};
            DIALOGUE::DialogueDefinition after{};
            uint64_t mergeGroup = 0u;
        };

        void ResetHistory(bool saved) noexcept;

        AssetGuid assetGuid_{};
        DIALOGUE::DialogueDefinition definition_{};
        std::vector<HistoryEntry> history_{};
        size_t historyCursor_ = 0u;
        std::optional<size_t> savedHistoryCursor_{};
        uint64_t openMergeGroup_ = 0u;
        bool open_ = false;
    };

} // namespace HIKARI::EDITOR
