#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceDocument.h"

#include <algorithm>
#include <utility>

#include "Assets/Material/Serialization/HIKARI_MaterialSourceJson.h"

namespace HIKARI::EDITOR {

void ModelMaterialWorkspaceDocument::OpenBindings(
    const AssetGuid &modelGuid, const AssetGuid &assetGuid,
    std::filesystem::path sourcePath, BindingMap bindings, bool saved) {
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
    MaterialAssetData source, const MaterialCompileResult &compile,
    const MaterialProgramIR *lastGoodProgram, bool saved) {
  materialAssetGuid_ = assetGuid;
  materialSourcePath_ = std::move(sourcePath);
  material_ = std::move(source);
  savedMaterial_ = material_;
  compile_ = compile;
  lastGoodProgram_.reset();
  if (lastGoodProgram != nullptr) {
    lastGoodProgram_ = *lastGoodProgram;
  } else if (compile_.success) {
    lastGoodProgram_ = compile_.program;
  }
  materialOpen_ = true;
  materialRevision_ = 1u;
  compileRevision_ = 1u;
  abBefore_.reset();
  materialStateToken_ = nextStateToken_++;
  savedMaterialStateToken_ = saved ? materialStateToken_ : 0u;
  ClearHistory(ChangedDomain::Material);
  activeHistoryDomain_ = ChangedDomain::Material;
}

void ModelMaterialWorkspaceDocument::CloseMaterial() {
  materialAssetGuid_ = {};
  materialSourcePath_.clear();
  material_ = {};
  savedMaterial_ = {};
  compile_ = {};
  lastGoodProgram_.reset();
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
  return bindingsOpen_;
}

bool ModelMaterialWorkspaceDocument::HasMaterial() const noexcept {
  return materialOpen_;
}

const AssetGuid &ModelMaterialWorkspaceDocument::GetModelGuid() const noexcept {
  return modelGuid_;
}

const AssetGuid &
ModelMaterialWorkspaceDocument::GetBindingAssetGuid() const noexcept {
  return bindingAssetGuid_;
}

const AssetGuid &
ModelMaterialWorkspaceDocument::GetMaterialAssetGuid() const noexcept {
  return materialAssetGuid_;
}

const std::filesystem::path &
ModelMaterialWorkspaceDocument::GetBindingSourcePath() const noexcept {
  return bindingSourcePath_;
}

const std::filesystem::path &
ModelMaterialWorkspaceDocument::GetMaterialSourcePath() const noexcept {
  return materialSourcePath_;
}

ModelMaterialWorkspaceDocument::BindingMap &
ModelMaterialWorkspaceDocument::EditBindings() noexcept {
  return bindings_;
}

const ModelMaterialWorkspaceDocument::BindingMap &
ModelMaterialWorkspaceDocument::GetBindings() const noexcept {
  return bindings_;
}

MaterialAssetData &ModelMaterialWorkspaceDocument::EditMaterial() noexcept {
  return material_;
}

const MaterialAssetData &
ModelMaterialWorkspaceDocument::GetMaterial() const noexcept {
  return material_;
}

const MaterialCompileResult &
ModelMaterialWorkspaceDocument::GetCompileResult() const noexcept {
  return compile_;
}

const MaterialProgramIR *
ModelMaterialWorkspaceDocument::GetLastGoodProgram() const noexcept {
  return lastGoodProgram_ ? &*lastGoodProgram_ : nullptr;
}

uint64_t ModelMaterialWorkspaceDocument::GetMaterialRevision() const noexcept {
  return materialRevision_;
}

uint64_t ModelMaterialWorkspaceDocument::GetCompileRevision() const noexcept {
  return compileRevision_;
}

bool ModelMaterialWorkspaceDocument::IsCompileCurrent() const noexcept {
  return materialOpen_ && compileRevision_ == materialRevision_;
}

bool ModelMaterialWorkspaceDocument::CommitBindings(BindingMap before,
                                                    std::string label) {
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
  if (!materialOpen_ || AreMaterialsEquivalent(before, material_)) {
    return false;
  }
  if (abBefore_) {
    ++materialRevision_;
    RefreshCompile();
    return true;
  }
  HistoryEntry entry{};
  entry.domain = ChangedDomain::Material;
  entry.beforeMaterial = std::move(before);
  entry.afterMaterial = material_;
  entry.beforeToken = materialStateToken_;
  entry.afterToken = nextStateToken_++;
  entry.label = std::move(label);
  materialStateToken_ = entry.afterToken;
  activeHistoryDomain_ = ChangedDomain::Material;
  PushHistory(std::move(entry));
  ++materialRevision_;
  RefreshCompile();
  return true;
}

void ModelMaterialWorkspaceDocument::RestoreMaterial(
    MaterialAssetData source) {
  if (!materialOpen_) {
    return;
  }
  material_ = std::move(source);
  ++materialRevision_;
  RefreshCompile();
}

bool ModelMaterialWorkspaceDocument::CanUndo() const noexcept {
  if (activeHistoryDomain_ == ChangedDomain::Binding) {
    return bindingHistoryCursor_ > 0u;
  }
  return activeHistoryDomain_ == ChangedDomain::Material && !abBefore_ &&
      materialHistoryCursor_ > 0u;
}

bool ModelMaterialWorkspaceDocument::CanRedo() const noexcept {
  if (activeHistoryDomain_ == ChangedDomain::Binding) {
    return bindingHistoryCursor_ < bindingHistory_.size();
  }
  return activeHistoryDomain_ == ChangedDomain::Material && !abBefore_ &&
      materialHistoryCursor_ < materialHistory_.size();
}

bool ModelMaterialWorkspaceDocument::Undo(ChangedDomain &outDomain,
                                          std::string &outLabel) {
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
  if (!CanUndo()) {
    return nullptr;
  }
  return activeHistoryDomain_ == ChangedDomain::Binding
      ? &bindingHistory_[bindingHistoryCursor_ - 1u].label
      : &materialHistory_[materialHistoryCursor_ - 1u].label;
}

const std::string *
ModelMaterialWorkspaceDocument::GetRedoLabel() const noexcept {
  if (!CanRedo()) {
    return nullptr;
  }
  return activeHistoryDomain_ == ChangedDomain::Binding
      ? &bindingHistory_[bindingHistoryCursor_].label
      : &materialHistory_[materialHistoryCursor_].label;
}

bool ModelMaterialWorkspaceDocument::IsBindingDirty() const noexcept {
  return bindingsOpen_ && bindingStateToken_ != savedBindingStateToken_;
}

bool ModelMaterialWorkspaceDocument::IsMaterialDirty() const noexcept {
  return materialOpen_ && materialStateToken_ != savedMaterialStateToken_;
}

bool ModelMaterialWorkspaceDocument::DiscardBindingChanges() noexcept {
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
  bindingAssetGuid_ = assetGuid;
  bindingSourcePath_ = std::move(sourcePath);
  savedBindingStateToken_ = bindingStateToken_;
  savedBindings_ = bindings_;
}

void ModelMaterialWorkspaceDocument::MarkMaterialSaved(
    const AssetGuid &assetGuid, std::filesystem::path sourcePath) noexcept {
  materialAssetGuid_ = assetGuid;
  materialSourcePath_ = std::move(sourcePath);
  savedMaterialStateToken_ = materialStateToken_;
  savedMaterial_ = material_;
}

bool ModelMaterialWorkspaceDocument::BeginAbPreview(
    MaterialAssetData candidate, std::string &outMessage) {
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
  return abBefore_.has_value();
}

bool ModelMaterialWorkspaceDocument::KeepAbPreview(std::string &outMessage) {
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
  compile_ = CompileMaterialAsset(material_, GetLastGoodProgram());
  if (compile_.success) {
    lastGoodProgram_ = compile_.program;
  }
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
