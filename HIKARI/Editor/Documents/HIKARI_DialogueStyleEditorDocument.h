#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

#include "Gameplay/Dialogue/HIKARI_DialogueProjectStyle.h"

namespace HIKARI { class AssetDatabase; }

namespace HIKARI::EDITOR {

    class DialogueStyleEditorDocument final {
    public:
        void New(
            const std::filesystem::path& projectRoot,
            const AssetDatabase& database);
        bool Open(
            const std::filesystem::path& projectRoot,
            const AssetDatabase& database,
            std::string& outMessage);
        bool Save(
            const std::filesystem::path& projectRoot,
            const AssetDatabase& database,
            std::string& outMessage);
        void DiscardChanges(
            const std::filesystem::path& projectRoot,
            const AssetDatabase& database);

        DIALOGUE::DialogueProjectStyle& Style() noexcept;
        const DIALOGUE::DialogueProjectStyle& Style() const noexcept;
        bool IsOpen() const noexcept;
        bool IsDirty() const noexcept;
        bool CanUndo() const noexcept;
        bool CanRedo() const noexcept;
        void RecordApplied(
            DIALOGUE::DialogueProjectStyle before,
            uint64_t mergeGroup = 0u);
        void SealMerge() noexcept;
        bool Undo();
        bool Redo();

    private:
        struct HistoryEntry {
            DIALOGUE::DialogueProjectStyle before{};
            DIALOGUE::DialogueProjectStyle after{};
            uint64_t mergeGroup = 0u;
        };

        void ResetHistory(bool saved) noexcept;

        DIALOGUE::DialogueProjectStyle style_{};
        std::vector<HistoryEntry> history_{};
        size_t historyCursor_ = 0u;
        std::optional<size_t> savedHistoryCursor_{};
        uint64_t openMergeGroup_ = 0u;
        bool open_ = false;
    };

} // namespace HIKARI::EDITOR
