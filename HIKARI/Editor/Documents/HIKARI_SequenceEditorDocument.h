#pragma once
#include "Editor/History/HIKARI_DocumentSnapshotHistory.h"
#include "Editor/Documents/HIKARI_EditorSourceVersion.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Core/HIKARI_AssetGuid.h"
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
    bool OpenAsset(const AssetDatabase& assetDatabase, const AssetGuid& assetGuid, std::string& outMessage);
    bool Save(AssetDatabase& assetDatabase, std::string& outMessage);
    bool SaveAs(AssetDatabase& assetDatabase, const CinematicSequence& sequence, std::string& outMessage);
    bool Revert(const AssetDatabase& assetDatabase, std::string& outMessage);
    bool RefreshCleanSource(const AssetDatabase&, std::string&);
    SceneCinematicsSettings& GetSettings(SceneCinematicsSettings& embeddedSettings) noexcept;
    const SceneCinematicsSettings& GetSettings(const SceneCinematicsSettings& embeddedSettings) const noexcept;
    CinematicSequence* GetAssetSequence() noexcept;
    const CinematicSequence* GetAssetSequence() const noexcept;

    void RecordApplied(SceneCinematicsSettings before, uint64_t mergeGroup);
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
    EditorSourceVersion sourceVersion_;

    void ResetHistory(bool saved) noexcept;
    void SetAssetSettings(CinematicSequence sequence);

    SequenceEditorSourceKind sourceKind_ = SequenceEditorSourceKind::EmbeddedScene;
    AssetGuid assetGuid_{};
    std::filesystem::path sourcePath_{};
    std::string displayName_{"Scene Cinematics"};
    SceneCinematicsSettings settings_;
    DocumentSnapshotHistory<SceneCinematicsSettings> history_;
};

} // namespace EDITOR
} // namespace HIKARI
