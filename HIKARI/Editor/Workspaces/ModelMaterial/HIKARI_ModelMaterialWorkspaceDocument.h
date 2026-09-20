#pragma once
#include "Editor/Documents/HIKARI_EditorSourceVersion.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Material/Compiler/HIKARI_MaterialCompiler.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"

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
  };

  void OpenBindings(const AssetGuid &modelGuid, const AssetGuid &assetGuid,
                    std::filesystem::path sourcePath, BindingMap bindings,
                    bool saved);
  void OpenMaterial(const AssetGuid &assetGuid,
                    std::filesystem::path sourcePath,
                    MaterialAssetData source,
                    const MaterialCompileResult &compile, bool saved);
  void SetMaterialCompileOptions(MaterialCompileOptions options);
  void CloseMaterial();

  bool HasBindings() const noexcept;
  bool HasMaterial() const noexcept;
  const AssetGuid &GetModelGuid() const noexcept;
  const AssetGuid &GetBindingAssetGuid() const noexcept;
  const AssetGuid &GetMaterialAssetGuid() const noexcept;
  const std::filesystem::path &GetBindingSourcePath() const noexcept;
  const std::filesystem::path &GetMaterialSourcePath() const noexcept;

  BindingMap &EditBindings() noexcept;
  BindingMap GetBindings() const;
  MaterialAssetData &EditMaterial() noexcept;
  MaterialAssetData GetMaterial() const;
  MaterialCompileResult GetCompileResult() const;
  uint64_t GetMaterialRevision() const noexcept;
  uint64_t GetCompileRevision() const noexcept;
  bool IsCompileCurrent() const noexcept;

  bool CommitBindings(BindingMap before, std::string label);
  bool CommitMaterial(MaterialAssetData before, std::string label);
  // Applies an edit and commits it inside the document lock. This is the only
  // safe material-mutation entry for worker threads (for example the AI
  // assistant); EditMaterial()/CommitMaterial() are reserved for the UI
  // thread's short-lived paired gestures.
  bool ApplyMaterialEdit(
      const std::function<void(MaterialAssetData &)> &edit,
      std::string label,
      std::string &outMessage);
  std::recursive_mutex &GetMutex() noexcept { return mutex_; }
  void RestoreMaterial(MaterialAssetData source);

  bool CanUndo() const noexcept;
  bool CanRedo() const noexcept;
  bool Undo(ChangedDomain &outDomain, std::string &outLabel);
  bool Redo(ChangedDomain &outDomain, std::string &outLabel);
  const std::string *GetUndoLabel() const noexcept;
  const std::string *GetRedoLabel() const noexcept;

  bool IsBindingDirty() const noexcept;
  bool IsMaterialDirty() const noexcept;
  bool DiscardBindingChanges() noexcept;
  bool DiscardMaterialChanges();
  void MarkBindingsSaved(const AssetGuid &assetGuid,
                         std::filesystem::path sourcePath) noexcept;
  void MarkMaterialSaved(const AssetGuid &assetGuid,
                         std::filesystem::path sourcePath) noexcept;

  bool BeginAbPreview(MaterialAssetData candidate, std::string &outMessage);
  bool IsAbPreviewActive() const noexcept;
  bool KeepAbPreview(std::string &outMessage);
  bool DiscardAbPreview(std::string &outMessage);

private:
  EditorSourceVersion materialSourceVersion_, bindingSourceVersion_;
  struct HistoryEntry {
    ChangedDomain domain = ChangedDomain::None;
    BindingMap beforeBindings{};
    BindingMap afterBindings{};
    MaterialAssetData beforeMaterial{};
    MaterialAssetData afterMaterial{};
    uint64_t beforeToken = 0u;
    uint64_t afterToken = 0u;
    std::string label{};
  };

  static constexpr size_t kMaximumHistoryEntries = 128u;
  static bool AreBindingsEquivalent(const BindingMap &left,
                                    const BindingMap &right) noexcept;
  static bool AreMaterialsEquivalent(const MaterialAssetData &left,
                                     const MaterialAssetData &right);
  bool CommitMaterialLocked(MaterialAssetData before, std::string label);
  void TruncateRedo(ChangedDomain domain);
  void PushHistory(HistoryEntry entry);
  void RefreshCompile();
  void ClearHistory(ChangedDomain domain) noexcept;

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
  std::optional<MaterialAssetData> abBefore_{};
  std::vector<HistoryEntry> bindingHistory_{};
  std::vector<HistoryEntry> materialHistory_{};
  size_t bindingHistoryCursor_ = 0u;
  size_t materialHistoryCursor_ = 0u;
  ChangedDomain activeHistoryDomain_ = ChangedDomain::None;
  uint64_t nextStateToken_ = 1u;
  uint64_t bindingStateToken_ = 0u;
  uint64_t materialStateToken_ = 0u;
  uint64_t savedBindingStateToken_ = 0u;
  uint64_t savedMaterialStateToken_ = 0u;
  bool bindingsOpen_ = false;
  bool materialOpen_ = false;
  uint64_t materialRevision_ = 0u;
  uint64_t compileRevision_ = 0u;
  mutable std::recursive_mutex mutex_{};
};

} // namespace HIKARI::EDITOR
