#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Vfx/HIKARI_VfxAssetData.h"
#include "Assets/Vfx/HIKARI_VfxPresetRegistry.h"

namespace HIKARI {

    class AssetDatabase;

namespace EDITOR {

    enum class VfxEditorDocumentSource : uint8_t {
        None,
        Transient,
        Asset,
    };

    class VfxEditorDocument final {
    public:
        void NewAsset(std::string name = "New VFX");
        bool ApplyPresetPackage(
            VFX::PRESETS::VfxPresetPackage package,
            std::string& outMessage);
        bool OpenAsset(
            const AssetDatabase& assetDatabase,
            const AssetGuid& assetGuid,
            std::string& outMessage,
            bool discardUnsaved = false);
        bool Save(
            AssetDatabase& assetDatabase,
            std::string& outMessage);
        bool Revert(
            const AssetDatabase& assetDatabase,
            std::string& outMessage);

        bool ApplyAsset(
            VFX::VfxAssetData asset,
            std::string& outMessage,
            uint64_t mergeGroup = 0u);
        void SealMerge() noexcept;
        bool Undo();
        bool Redo();

        VFX::VfxAssetData& Asset() noexcept;
        const VFX::VfxAssetData& Asset() const noexcept;
        const AssetGuid& GetAssetGuid() const noexcept;
        const std::filesystem::path& GetSourcePath() const noexcept;
        const std::string& GetDisplayName() const noexcept;
        uint64_t GetRevision() const noexcept;
        VfxEditorDocumentSource GetSource() const noexcept;

        bool IsOpen() const noexcept;
        bool IsDirty() const noexcept;
        bool CanUndo() const noexcept;
        bool CanRedo() const noexcept;
        bool HasPendingPresetMaterials() const noexcept;
        const std::vector<VFX::PRESETS::VfxPresetMaterialRecipe>&
            GetPendingPresetMaterials() const noexcept;

    private:
        struct DocumentState {
            VFX::VfxAssetData asset{};
            std::vector<VFX::PRESETS::VfxPresetMaterialRecipe>
                pendingPresetMaterials{};
        };

        struct HistoryEntry {
            DocumentState before{};
            DocumentState after{};
            uint64_t mergeGroup = 0u;
        };

        static bool AreEquivalent(
            const VFX::VfxAssetData& left,
            const VFX::VfxAssetData& right);
        bool ApplyState(
            DocumentState state,
            std::string& outMessage,
            uint64_t mergeGroup);
        void RecordApplied(
            DocumentState before,
            uint64_t mergeGroup);
        void ResetHistory(bool saved) noexcept;
        void AdvanceRevision() noexcept;

        VfxEditorDocumentSource source_ =
            VfxEditorDocumentSource::None;
        AssetGuid assetGuid_{};
        std::filesystem::path sourcePath_{};
        std::string displayName_{};
        VFX::VfxAssetData asset_{};
        std::vector<VFX::PRESETS::VfxPresetMaterialRecipe>
            pendingPresetMaterials_{};
        std::vector<HistoryEntry> history_{};
        size_t historyCursor_ = 0u;
        std::optional<size_t> savedHistoryCursor_{};
        uint64_t openMergeGroup_ = 0u;
        uint64_t revision_ = 1u;
    };

} // namespace EDITOR
} // namespace HIKARI
