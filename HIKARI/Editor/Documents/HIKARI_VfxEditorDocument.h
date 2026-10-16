#pragma once
#include "Editor/Documents/HIKARI_EditorSourceVersion.h"
#include "Editor/History/HIKARI_DocumentSnapshotHistory.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <json.hpp>

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

enum class VfxDocumentImpact : uint8_t { Metadata, Runtime };

class VfxEditorDocument final {
public:
    void NewAsset(std::string name = "New VFX");
    bool ApplyPresetAsset(VFX::VfxAssetData package, std::string& outMessage);
    bool OpenAsset(const AssetDatabase& assetDatabase, const AssetGuid& assetGuid, std::string& outMessage,
                   bool discardUnsaved = false);
    bool Save(AssetDatabase& assetDatabase, std::string& outMessage);
    bool Revert(const AssetDatabase& assetDatabase, std::string& outMessage);

    bool ApplyAsset(VFX::VfxAssetData asset, std::string& outMessage, uint64_t mergeGroup = 0u,
                    VfxDocumentImpact impact = VfxDocumentImpact::Runtime);
    void SealMerge() noexcept;
    bool Undo();
    bool Redo();

    const VFX::VfxAssetData& Asset() const noexcept { return asset_; }
    const AssetGuid& GetAssetGuid() const noexcept;
    const std::filesystem::path& GetSourcePath() const noexcept;
    const std::string& GetDisplayName() const noexcept;
    uint64_t GetRevision() const noexcept;
    VfxDocumentImpact GetLastImpact() const noexcept { return lastImpact_; }
    VfxEditorDocumentSource GetSource() const noexcept;

    bool IsOpen() const noexcept;
    bool IsDirty() const noexcept;
    bool CanUndo() const noexcept;
    bool CanRedo() const noexcept;
    nlohmann::json MakeRecovery() const;
    bool RestoreRecovery(const AssetDatabase& database, const nlohmann::json& snapshot, std::string& message);

private:
    EditorSourceVersion sourceVersion_;
    static bool AreEquivalent(const VFX::VfxAssetData& left, const VFX::VfxAssetData& right);
    void RecordApplied(VFX::VfxAssetData before, uint64_t mergeGroup,
                       VfxDocumentImpact impact = VfxDocumentImpact::Runtime);
    void ResetHistory(bool saved) noexcept;

    VfxEditorDocumentSource source_ = VfxEditorDocumentSource::None;
    AssetGuid assetGuid_{};
    std::filesystem::path sourcePath_{};
    std::string displayName_{};
    VFX::VfxAssetData asset_{};
    DocumentSnapshotHistory<VFX::VfxAssetData, VfxDocumentImpact> history_;
    uint64_t revision_ = 1u;
    VfxDocumentImpact lastImpact_ = VfxDocumentImpact::Runtime;
};

} // namespace EDITOR
} // namespace HIKARI
