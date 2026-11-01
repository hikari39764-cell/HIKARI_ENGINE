#pragma once
#include "Editor/Documents/HIKARI_EditorSourceVersion.h"
#include "Editor/History/HIKARI_DocumentSnapshotHistory.h"

#include <cstddef>
#include <filesystem>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/HIKARI_AssetGuid.h"
#include "Assets/Material/Compiler/HIKARI_MaterialCompiler.h"
#include "RenderContracts/Material/HIKARI_MaterialAssetData.h"

namespace HIKARI::EDITOR {

class ModelMaterialWorkspaceDocument {
public:
    bool CanSaveMaterialSource(std::string& message) const { return materialSourceVersion_.CanSave(message); }
    bool CanSaveBindingSource(std::string& message) const { return bindingSourceVersion_.CanSave(message); }
    void AcceptMaterialSourceWrite(const std::filesystem::path& path) { materialSourceVersion_.Accept(path); }
    using BindingMap = std::unordered_map<uint64_t, AssetGuid>;

    enum class ChangedDomain : uint8_t {
        None,
        Binding,
        Material,
        MaterialLayout,
    };

    void OpenBindings(const AssetGuid& modelGuid, const AssetGuid& assetGuid, std::filesystem::path sourcePath,
                      BindingMap bindings, bool saved);
    void OpenMaterial(const AssetGuid& assetGuid, std::filesystem::path sourcePath, MaterialAssetData source,
                      const MaterialCompileResult& compile, bool saved);
    void SetMaterialCompileOptions(MaterialCompileOptions options);
    void CloseMaterial();

    bool HasBindings() const noexcept;
    bool HasMaterial() const noexcept;
    const AssetGuid& GetModelGuid() const noexcept;
    const AssetGuid& GetBindingAssetGuid() const noexcept;
    const AssetGuid& GetMaterialAssetGuid() const noexcept;
    const std::filesystem::path& GetBindingSourcePath() const noexcept;
    const std::filesystem::path& GetMaterialSourcePath() const noexcept;

    BindingMap GetBindings() const;
    MaterialAssetData GetMaterial() const;
    MaterialCompileResult GetCompileResult() const;
    uint64_t GetMaterialRevision() const noexcept;
    uint64_t GetCompileRevision() const noexcept;
    bool IsCompileCurrent() const noexcept;
    bool ConsumeMaterialPreviewChange() noexcept;

    bool ApplyMaterial(MaterialAssetData source, std::string label);
    bool ConnectMaterialNodes(MaterialGraphEdgeData edge, std::string& outMessage);
    bool RemoveMaterialNodes(std::span<const std::string> ids);
    bool DisconnectMaterialEdge(std::string_view id);
    void BeginMaterialGesture(MaterialAssetData before, std::string label);
    bool HasMaterialGesture() const noexcept;
    bool EndMaterialGesture(bool cancel);
    void UpdateMaterialItem(MaterialAssetData before, std::string label, uint32_t itemId, bool active, bool changed);
    void FinishMaterialItems(uint32_t activeItem);
    std::recursive_mutex& GetMutex() noexcept { return mutex_; }

    bool CanUndo() const noexcept;
    bool CanRedo() const noexcept;
    bool Undo(ChangedDomain& outDomain, std::string& outLabel);
    bool Redo(ChangedDomain& outDomain, std::string& outLabel);
    const std::string* GetUndoLabel() const noexcept;
    const std::string* GetRedoLabel() const noexcept;

    bool IsBindingDirty() const noexcept;
    bool IsMaterialDirty() const noexcept;
    bool DiscardBindingChanges() noexcept;
    bool DiscardMaterialChanges();
    void MarkBindingsSaved(const AssetGuid& assetGuid, std::filesystem::path sourcePath) noexcept;
    void MarkMaterialSaved(const AssetGuid& assetGuid, std::filesystem::path sourcePath) noexcept;

    bool BeginAbPreview(MaterialAssetData candidate, std::string& outMessage);
    bool IsAbPreviewActive() const noexcept;
    bool KeepAbPreview(std::string& outMessage);
    bool DiscardAbPreview(std::string& outMessage);

private:
    friend class ModelMaterialWorkspaceController;
    // 即時 UI の参照は Draw のロック内だけで使用する。外部編集は値を Apply する。
    BindingMap& EditBindings() noexcept;
    MaterialAssetData& EditMaterial() noexcept;
    bool CommitBindings(BindingMap before, std::string label);
    bool CommitMaterial(MaterialAssetData before, std::string label);
    void RestoreMaterial(MaterialAssetData source);
    EditorSourceVersion materialSourceVersion_, bindingSourceVersion_;
    struct HistoryMetadata {
        ChangedDomain domain = ChangedDomain::None;
        std::string label;
    };

    static bool AreBindingsEquivalent(const BindingMap& left, const BindingMap& right) noexcept;
    static bool AreMaterialsEquivalent(const MaterialAssetData& left, const MaterialAssetData& right);
    bool CommitMaterialLocked(MaterialAssetData before, std::string label);
    MaterialGraphData* EditableGraph() noexcept;
    void RecordMaterialChange(const MaterialAssetData& before, const MaterialAssetData& after, std::string label);
    bool TravelHistory(bool redo, ChangedDomain& domain, std::string& label);
    void RefreshCompile();
    void RefreshAfterMaterialChange(bool contentChanged);
    static bool AreMaterialContentsEquivalent(const MaterialAssetData& left, const MaterialAssetData& right);

    AssetGuid modelGuid_{};
    AssetGuid bindingAssetGuid_{};
    AssetGuid materialAssetGuid_{};
    std::filesystem::path bindingSourcePath_{};
    std::filesystem::path materialSourcePath_{};
    BindingMap bindings_{};
    BindingMap savedBindings_{};
    MaterialAssetData material_{};
    MaterialAssetData savedMaterial_{};
    MaterialCompileResult compile_{};
    MaterialCompileOptions compileOptions_{};
    struct PreviewEdit {
        MaterialAssetData before;
        std::string label;
        bool audition = false;
        uint32_t itemId = 0;
    };
    std::optional<PreviewEdit> previewEdit_;
    DocumentSnapshotHistory<BindingMap, HistoryMetadata> bindingHistory_{128};
    DocumentSnapshotHistory<MaterialAssetData, HistoryMetadata> materialHistory_{128};
    ChangedDomain activeHistoryDomain_ = ChangedDomain::None;
    bool bindingsOpen_ = false;
    bool materialOpen_ = false;
    uint64_t materialRevision_ = 0u;
    uint64_t compileRevision_ = 0u;
    bool materialPreviewPending_ = false;
    mutable std::recursive_mutex mutex_{};
};

} // namespace HIKARI::EDITOR
