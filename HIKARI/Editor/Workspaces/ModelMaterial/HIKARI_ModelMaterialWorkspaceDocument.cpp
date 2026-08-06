#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceDocument.h"

#include <algorithm>
#include <utility>

#include "Assets/Material/Serialization/HIKARI_MaterialSourceJson.h"

namespace HIKARI::EDITOR {

void ModelMaterialWorkspaceDocument::OpenBindings(
    const AssetGuid &modelGuid, const AssetGuid &assetGuid,
    std::filesystem::path sourcePath, BindingMap bindings, bool saved) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  modelGuid_ = modelGuid;
  bindingAssetGuid_ = assetGuid;
  bindingSourcePath_ = std::move(sourcePath);
  bindings_ = std::move(bindings);
  savedBindings_ = bindings_;
  bindingsOpen_ = modelGuid_.IsValid();
  bindingStateToken_ = nextStateToken_++;
  savedBindingStateToken_ = saved ? bindingStateToken_ : 0u;
  ClearHistory(ChangedDomain::Binding);
  activeHistoryDomain_ = ChangedDomain::Binding;
}

void ModelMaterialWorkspaceDocument::OpenMaterial(
    const AssetGuid &assetGuid, std::filesystem::path sourcePath,
    MaterialAssetData source, const MaterialCompileResult &compile, bool saved) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  materialAssetGuid_ = assetGuid;
  materialSourcePath_ = std::move(sourcePath);
  material_ = std::move(source);
  savedMaterial_ = material_;
  compile_ = compile;
  materialOpen_ = true;
  materialRevision_ = 1u;
  compileRevision_ = 1u;
  abBefore_.reset();
  materialStateToken_ = nextStateToken_++;
  savedMaterialStateToken_ = saved ? materialStateToken_ : 0u;
  ClearHistory(ChangedDomain::Material);
  activeHistoryDomain_ = ChangedDomain::Material;
}

void ModelMaterialWorkspaceDocument::SetMaterialCompileOptions(
    MaterialCompileOptions options) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  compileOptions_ = std::move(options);
  if (materialOpen_) {
    RefreshCompile();
  }
}

void ModelMaterialWorkspaceDocument::CloseMaterial() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  materialAssetGuid_ = {};
  materialSourcePath_.clear();
  material_ = {};
  savedMaterial_ = {};
  compile_ = {};
  abBefore_.reset();
  materialOpen_ = false;
  materialRevision_ = 0u;
  compileRevision_ = 0u;
  materialStateToken_ = 0u;
  savedMaterialStateToken_ = 0u;
  ClearHistory(ChangedDomain::Material);
  activeHistoryDomain_ = bindingsOpen_
      ? ChangedDomain::Binding
      : ChangedDomain::None;
}

bool ModelMaterialWorkspaceDocument::HasBindings() const noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return bindingsOpen_;
}

bool ModelMaterialWorkspaceDocument::HasMaterial() const noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return materialOpen_;
}

const AssetGuid &ModelMaterialWorkspaceDocument::GetModelGuid() const noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return modelGuid_;
}

const AssetGuid &
ModelMaterialWorkspaceDocument::GetBindingAssetGuid() const noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return bindingAssetGuid_;
}

const AssetGuid &
ModelMaterialWorkspaceDocument::GetMaterialAssetGuid() const noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return materialAssetGuid_;
}

const std::filesystem::path &
ModelMaterialWorkspaceDocument::GetBindingSourcePath() const noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return bindingSourcePath_;
}

const std::filesystem::path &
ModelMaterialWorkspaceDocument::GetMaterialSourcePath() const noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return materialSourcePath_;
}

ModelMaterialWorkspaceDocument::BindingMap &
ModelMaterialWorkspaceDocument::EditBindings() noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return bindings_;
}

ModelMaterialWorkspaceDocument::BindingMap
ModelMaterialWorkspaceDocument::GetBindings() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return bindings_;
}

MaterialAssetData &ModelMaterialWorkspaceDocument::EditMaterial() noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return material_;
}

MaterialAssetData ModelMaterialWorkspaceDocument::GetMaterial() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return material_;
}

MaterialCompileResult ModelMaterialWorkspaceDocument::GetCompileResult() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return compile_;
}

uint64_t ModelMaterialWorkspaceDocument::GetMaterialRevision() const noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return materialRevision_;
}

uint64_t ModelMaterialWorkspaceDocument::GetCompileRevision() const noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return compileRevision_;
}

bool ModelMaterialWorkspaceDocument::IsCompileCurrent() const noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return materialOpen_ && compileRevision_ == materialRevision_;
}

bool ModelMaterialWorkspaceDocument::CommitBindings(BindingMap before,
                                                    std::string label) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!bindingsOpen_ || AreBindingsEquivalent(before, bindings_)) {
    return false;
  }
  HistoryEntry entry{};
  entry.domain = ChangedDomain::Binding;
  entry.beforeBindings = std::move(before);
  entry.afterBindings = bindings_;
  entry.beforeToken = bindingStateToken_;
  entry.afterToken = nextStateToken_++;
  entry.label = std::move(label);
  bindingStateToken_ = entry.afterToken;
  activeHistoryDomain_ = ChangedDomain::Binding;
  PushHistory(std::move(entry));
  return true;
}

bool ModelMaterialWorkspaceDocument::CommitMaterial(MaterialAssetData before,
                                                    std::string label) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return CommitMaterialLocked(std::move(before), std::move(label));
}

void ModelMaterialWorkspaceDocument::RestoreMaterial(
    MaterialAssetData source) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!materialOpen_) {
    return;
  }
  material_ = std::move(source);
  ++materialRevision_;
  RefreshCompile();
}

bool ModelMaterialWorkspaceDocument::CanUndo() const noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (activeHistoryDomain_ == ChangedDomain::Binding) {
    return bindingHistoryCursor_ > 0u;
  }
  return activeHistoryDomain_ == ChangedDomain::Material && !abBefore_ &&
      materialHistoryCursor_ > 0u;
}

bool ModelMaterialWorkspaceDocument::CanRedo() const noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (activeHistoryDomain_ == ChangedDomain::Binding) {
    return bindingHistoryCursor_ < bindingHistory_.size();
  }
  return activeHistoryDomain_ == ChangedDomain::Material && !abBefore_ &&
      materialHistoryCursor_ < materialHistory_.size();
}

bool ModelMaterialWorkspaceDocument::Undo(ChangedDomain &outDomain,
                                          std::string &outLabel) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!CanUndo()) {
    return false;
  }
  std::vector<HistoryEntry> &history =
      activeHistoryDomain_ == ChangedDomain::Binding
          ? bindingHistory_
          : materialHistory_;
  size_t &cursor = activeHistoryDomain_ == ChangedDomain::Binding
      ? bindingHistoryCursor_
      : materialHistoryCursor_;
  HistoryEntry &entry = history[--cursor];
  outDomain = entry.domain;
  outLabel = entry.label;
  if (entry.domain == ChangedDomain::Binding) {
    bindings_ = entry.beforeBindings;
    bindingStateToken_ = entry.beforeToken;
  } else if (entry.domain == ChangedDomain::Material) {
    material_ = entry.beforeMaterial;
    materialStateToken_ = entry.beforeToken;
    ++materialRevision_;
    RefreshCompile();
  }
  return true;
}

bool ModelMaterialWorkspaceDocument::Redo(ChangedDomain &outDomain,
                                          std::string &outLabel) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!CanRedo()) {
    return false;
  }
  std::vector<HistoryEntry> &history =
      activeHistoryDomain_ == ChangedDomain::Binding
          ? bindingHistory_
          : materialHistory_;
  size_t &cursor = activeHistoryDomain_ == ChangedDomain::Binding
      ? bindingHistoryCursor_
      : materialHistoryCursor_;
  HistoryEntry &entry = history[cursor++];
  outDomain = entry.domain;
  outLabel = entry.label;
  if (entry.domain == ChangedDomain::Binding) {
    bindings_ = entry.afterBindings;
    bindingStateToken_ = entry.afterToken;
  } else if (entry.domain == ChangedDomain::Material) {
    material_ = entry.afterMaterial;
    materialStateToken_ = entry.afterToken;
    ++materialRevision_;
    RefreshCompile();
  }
  return true;
}

const std::string *
ModelMaterialWorkspaceDocument::GetUndoLabel() const noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!CanUndo()) {
    return nullptr;
  }
  return activeHistoryDomain_ == ChangedDomain::Binding
      ? &bindingHistory_[bindingHistoryCursor_ - 1u].label
      : &materialHistory_[materialHistoryCursor_ - 1u].label;
}

const std::string *
ModelMaterialWorkspaceDocument::GetRedoLabel() const noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!CanRedo()) {
    return nullptr;
  }
  return activeHistoryDomain_ == ChangedDomain::Binding
      ? &bindingHistory_[bindingHistoryCursor_].label
      : &materialHistory_[materialHistoryCursor_].label;
}

bool ModelMaterialWorkspaceDocument::IsBindingDirty() const noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return bindingsOpen_ && bindingStateToken_ != savedBindingStateToken_;
}

bool ModelMaterialWorkspaceDocument::IsMaterialDirty() const noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return materialOpen_ && materialStateToken_ != savedMaterialStateToken_;
}

bool ModelMaterialWorkspaceDocument::DiscardBindingChanges() noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!bindingsOpen_ || !IsBindingDirty()) {
    return false;
  }
  bindings_ = savedBindings_;
  bindingStateToken_ = savedBindingStateToken_;
  ClearHistory(ChangedDomain::Binding);
  activeHistoryDomain_ = ChangedDomain::Binding;
  return true;
}

bool ModelMaterialWorkspaceDocument::DiscardMaterialChanges() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!materialOpen_ || (!IsMaterialDirty() && !abBefore_)) {
    return false;
  }
  material_ = savedMaterial_;
  materialStateToken_ = savedMaterialStateToken_;
  abBefore_.reset();
  ClearHistory(ChangedDomain::Material);
  activeHistoryDomain_ = ChangedDomain::Material;
  ++materialRevision_;
  RefreshCompile();
  return true;
}

void ModelMaterialWorkspaceDocument::MarkBindingsSaved(
    const AssetGuid &assetGuid, std::filesystem::path sourcePath) noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  bindingAssetGuid_ = assetGuid;
  bindingSourcePath_ = std::move(sourcePath);
  savedBindingStateToken_ = bindingStateToken_;
  savedBindings_ = bindings_;
}

void ModelMaterialWorkspaceDocument::MarkMaterialSaved(
    const AssetGuid &assetGuid, std::filesystem::path sourcePath) noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  materialAssetGuid_ = assetGuid;
  materialSourcePath_ = std::move(sourcePath);
  savedMaterialStateToken_ = materialStateToken_;
  savedMaterial_ = material_;
}

bool ModelMaterialWorkspaceDocument::BeginAbPreview(
    MaterialAssetData candidate, std::string &outMessage) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!materialOpen_) {
    outMessage = "open or create a material before starting A/B preview";
    return false;
  }
  if (abBefore_) {
    outMessage = "finish the active A/B preview first";
    return false;
  }
  abBefore_ = material_;
  material_ = std::move(candidate);
  ++materialRevision_;
  RefreshCompile();
  outMessage = "B variant is preview-only; Keep commits one undo step";
  return true;
}

bool ModelMaterialWorkspaceDocument::IsAbPreviewActive() const noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return abBefore_.has_value();
}

bool ModelMaterialWorkspaceDocument::KeepAbPreview(std::string &outMessage) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!abBefore_) {
    outMessage = "no A/B preview is active";
    return false;
  }
  MaterialAssetData before = std::move(*abBefore_);
  abBefore_.reset();
  const bool changed = CommitMaterial(std::move(before), "Keep A/B material");
  outMessage = changed ? "kept B variant as one material edit"
                       : "B variant matches the authored material";
  return changed;
}

bool ModelMaterialWorkspaceDocument::DiscardAbPreview(
    std::string &outMessage) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!abBefore_) {
    outMessage = "no A/B preview is active";
    return false;
  }
  material_ = std::move(*abBefore_);
  abBefore_.reset();
  ++materialRevision_;
  RefreshCompile();
  outMessage = "discarded B variant without changing the asset";
  return true;
}

bool ModelMaterialWorkspaceDocument::AreBindingsEquivalent(
    const BindingMap &left, const BindingMap &right) noexcept {
  if (left.size() != right.size()) {
    return false;
  }
  for (const auto &[slotId, guid] : left) {
    const auto found = right.find(slotId);
    if (found == right.end() || found->second != guid) {
      return false;
    }
  }
  return true;
}

bool ModelMaterialWorkspaceDocument::AreMaterialsEquivalent(
    const MaterialAssetData &left, const MaterialAssetData &right) {
  std::string leftText{};
  std::string rightText{};
  std::string error{};
  return MATERIAL_SERIALIZATION::SerializeMaterialSourceJson(
             left, leftText, error) &&
         MATERIAL_SERIALIZATION::SerializeMaterialSourceJson(
             right, rightText, error) &&
         leftText == rightText;
}

void ModelMaterialWorkspaceDocument::TruncateRedo(ChangedDomain domain) {
  std::vector<HistoryEntry> &history = domain == ChangedDomain::Binding
      ? bindingHistory_
      : materialHistory_;
  size_t &cursor = domain == ChangedDomain::Binding
      ? bindingHistoryCursor_
      : materialHistoryCursor_;
  if (cursor < history.size()) {
    history.erase(history.begin() + static_cast<std::ptrdiff_t>(cursor),
                  history.end());
  }
}

void ModelMaterialWorkspaceDocument::PushHistory(HistoryEntry entry) {
  const ChangedDomain domain = entry.domain;
  TruncateRedo(domain);
  std::vector<HistoryEntry> &history = domain == ChangedDomain::Binding
      ? bindingHistory_
      : materialHistory_;
  size_t &cursor = domain == ChangedDomain::Binding
      ? bindingHistoryCursor_
      : materialHistoryCursor_;
  history.push_back(std::move(entry));
  cursor = history.size();
  if (history.size() <= kMaximumHistoryEntries) {
    return;
  }
  history.erase(history.begin());
  --cursor;
}

void ModelMaterialWorkspaceDocument::RefreshCompile() {
  // Material v5 authoring is graph-authoritative. A failed edit must remain a
  // failed current graph; projecting any stale program here would make the
  // viewport look live while it is actually rendering an older topology.
  compile_ = CompileMaterialAsset(material_, compileOptions_);
  compileRevision_ = materialRevision_;
}

void ModelMaterialWorkspaceDocument::ClearHistory(
    ChangedDomain domain) noexcept {
  if (domain == ChangedDomain::Binding) {
    bindingHistory_.clear();
    bindingHistoryCursor_ = 0u;
    return;
  }
  materialHistory_.clear();
  materialHistoryCursor_ = 0u;
}

} // namespace HIKARI::EDITOR
