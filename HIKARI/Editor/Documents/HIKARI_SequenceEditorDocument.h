#pragma once
#include "Editor/History/HIKARI_DocumentSnapshotHistory.h"
#include "Editor/Documents/HIKARI_EditorSourceVersion.h"

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

// 区域を共有する端点。順序の先頭は現在の文書の参照元で、正逆の再生方向には依存しない。
struct SequenceCameraEndpoint {
    AssetGuid assetGuid;
    SEQUENCER::SequenceBindingId bindingId;
    bool end = false;
    bool operator==(const SequenceCameraEndpoint&) const = default;
};
struct SequenceCameraEndpointGroup {
    std::string label;
    std::vector<SequenceCameraEndpoint> endpoints;
    bool operator==(const SequenceCameraEndpointGroup&) const = default;
};

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
    bool SetCameraEndpointGroups(const AssetDatabase&, const std::vector<SequenceCameraEndpointGroup>&,
                                 std::string label, bool atEnd, std::string&);
    void ClearCameraEndpoints();
    const std::string& GetCameraEndpointLabel() const noexcept { return endpointLabel_; }
    float GetCameraEndpointTime() const;
    const std::vector<SequenceCameraEndpointGroup>& GetCameraEndpointGroups() const noexcept { return endpointGroups_; }
    const SequenceCameraEndpointGroup* FindCameraEndpointGroup(SEQUENCER::SequenceBindingId, float time) const;
    bool CameraEndpointsMatch(const SequenceCameraEndpointGroup&) const;
    bool AlignCameraEndpoint(SEQUENCER::SequenceBindingId, bool end, std::string&);

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
    // 主文書と関連端点の草稿を同じ履歴に含め、端点を変更しない「統一」も取り消せる。
    struct State {
        SceneCinematicsSettings settings;
        std::vector<CinematicSequence> linked;
    } state_;
    DocumentSnapshotHistory<State> history_;
    struct LinkedSource {
        AssetGuid guid;
        CinematicSequence saved;
        EditorSourceVersion version;
    };
    std::vector<LinkedSource> linkedSources_;
    std::vector<SequenceCameraEndpointGroup> endpointGroups_;
    std::string endpointLabel_;
    bool endpointAtEnd_ = false;
    bool ApplyCameraEndpoint(const SequenceCameraEndpointGroup&);
    const CinematicSequence* EndpointSequence(const SequenceCameraEndpoint&) const;
    void SyncChangedCameraEndpoints(const SceneCinematicsSettings& before);
    bool SaveCameraEndpoints(AssetDatabase&, std::string&);
};

} // namespace EDITOR
} // namespace HIKARI
