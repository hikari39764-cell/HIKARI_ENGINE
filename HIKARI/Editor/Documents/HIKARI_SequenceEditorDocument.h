#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Scene/HIKARI_CinematicSequence.h"

namespace HIKARI {

    class AssetDatabase;

namespace EDITOR {

    enum class SequenceEditorSourceKind : uint8_t {
        EmbeddedScene,
        TransientAsset,
        Asset,
    };

    class SequenceEditorDocument {
    public:
        void OpenEmbeddedScene() noexcept;
        void NewAsset(std::string name = "New Sequence");
        bool OpenAsset(
            const AssetDatabase& assetDatabase,
            const AssetGuid& assetGuid,
            std::string& outMessage);
        bool Save(
            AssetDatabase& assetDatabase,
            std::string& outMessage);
        bool SaveAs(
            AssetDatabase& assetDatabase,
            const CinematicSequence& sequence,
            std::string& outMessage);
        bool Revert(
            const AssetDatabase& assetDatabase,
            std::string& outMessage);

        SceneCinematicsSettings& GetSettings(
            SceneCinematicsSettings& embeddedSettings) noexcept;
        const SceneCinematicsSettings& GetSettings(
            const SceneCinematicsSettings& embeddedSettings) const noexcept;
        CinematicSequence* GetAssetSequence() noexcept;
        const CinematicSequence* GetAssetSequence() const noexcept;

        void RecordApplied(
            SceneCinematicsSettings before,
            uint64_t mergeGroup);
        void SealMerge() noexcept;
        bool Undo();
        bool Redo();
        bool CanUndo() const noexcept;
        bool CanRedo() const noexcept;
        bool IsDirty() const noexcept;

        SequenceEditorSourceKind GetSourceKind() const noexcept;
        bool IsEmbeddedScene() const noexcept;
        bool IsExternalDocument() const noexcept;
        const AssetGuid& GetAssetGuid() const noexcept;
        const std::filesystem::path& GetSourcePath() const noexcept;
        const std::string& GetDisplayName() const noexcept;

    private:
        struct HistoryEntry {
            SceneCinematicsSettings before{};
            SceneCinematicsSettings after{};
            uint64_t mergeGroup = 0;
        };

        void ResetHistory(bool saved) noexcept;
        void SetAssetSettings(CinematicSequence sequence);

        SequenceEditorSourceKind sourceKind_ =
            SequenceEditorSourceKind::EmbeddedScene;
        AssetGuid assetGuid_{};
        std::filesystem::path sourcePath_{};
        std::string displayName_{ "Scene Cinematics" };
        SceneCinematicsSettings assetSettings_{};
        std::vector<HistoryEntry> history_{};
        size_t historyCursor_ = 0;
        std::optional<size_t> savedHistoryCursor_{ 0 };
        uint64_t openMergeGroup_ = 0;
    };

} // namespace EDITOR
} // namespace HIKARI
