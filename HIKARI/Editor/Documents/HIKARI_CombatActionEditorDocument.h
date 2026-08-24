#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "Assets/Action/HIKARI_CombatActionAsset.h"

namespace HIKARI { class AssetDatabase; }

namespace HIKARI::EDITOR {

    class CombatActionEditorDocument {
    public:
        void New(std::string name = "New Combat Action");
        bool Open(
            const AssetDatabase& database,
            const AssetGuid& guid,
            std::string& outMessage);
        bool Save(AssetDatabase& database, std::string& outMessage);
        bool Revert(const AssetDatabase& database, std::string& outMessage);

        ACTION::CombatActionDefinition& Definition() noexcept;
        const ACTION::CombatActionDefinition& Definition() const noexcept;
        const AssetGuid& GetAssetGuid() const noexcept;
        bool IsOpen() const noexcept;
        bool IsDirty() const noexcept;
        bool CanUndo() const noexcept;
        bool CanRedo() const noexcept;
        void RecordApplied(
            ACTION::CombatActionDefinition before,
            uint64_t mergeGroup = 0u);
        void SealMerge() noexcept;
        bool Undo();
        bool Redo();

    private:
        struct HistoryEntry {
            ACTION::CombatActionDefinition before{};
            ACTION::CombatActionDefinition after{};
            uint64_t mergeGroup = 0u;
        };

        void ResetHistory(bool saved) noexcept;

        AssetGuid assetGuid_{};
        ACTION::CombatActionDefinition definition_{};
        std::vector<HistoryEntry> history_{};
        size_t historyCursor_ = 0u;
        std::optional<size_t> savedHistoryCursor_{};
        uint64_t openMergeGroup_ = 0u;
        bool open_ = false;
    };

} // namespace HIKARI::EDITOR
