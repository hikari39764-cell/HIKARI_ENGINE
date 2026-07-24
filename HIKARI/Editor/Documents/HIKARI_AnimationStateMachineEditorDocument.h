#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "Animation/StateMachine/HIKARI_AnimationStateMachine.h"
#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI { class AssetDatabase; }

namespace HIKARI::EDITOR {

    class AnimationStateMachineEditorDocument {
    public:
        void New(std::string name = "New Animation State Machine");
        bool Open(
            const AssetDatabase& database,
            const AssetGuid& guid,
            std::string& outMessage);
        bool Save(AssetDatabase& database, std::string& outMessage);
        bool Revert(const AssetDatabase& database, std::string& outMessage);

        ANIMATION::AnimationStateMachineDefinition& Definition() noexcept;
        const ANIMATION::AnimationStateMachineDefinition& Definition()
            const noexcept;
        const AssetGuid& GetAssetGuid() const noexcept;
        bool IsOpen() const noexcept;
        bool IsDirty() const noexcept;
        bool CanUndo() const noexcept;
        bool CanRedo() const noexcept;
        void RecordApplied(
            ANIMATION::AnimationStateMachineDefinition before,
            uint64_t mergeGroup = 0u);
        void SealMerge() noexcept;
        bool Undo();
        bool Redo();

    private:
        struct HistoryEntry {
            ANIMATION::AnimationStateMachineDefinition before{};
            ANIMATION::AnimationStateMachineDefinition after{};
            uint64_t mergeGroup = 0u;
        };

        void ResetHistory(bool saved) noexcept;

        AssetGuid assetGuid_{};
        ANIMATION::AnimationStateMachineDefinition definition_{};
        std::vector<HistoryEntry> history_{};
        size_t historyCursor_ = 0u;
        std::optional<size_t> savedHistoryCursor_{};
        uint64_t openMergeGroup_ = 0u;
        bool open_ = false;
    };

} // namespace HIKARI::EDITOR
