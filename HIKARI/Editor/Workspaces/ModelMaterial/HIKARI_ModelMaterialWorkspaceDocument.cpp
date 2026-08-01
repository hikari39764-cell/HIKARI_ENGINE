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
  bindingsOpen_ = modelGuid_.IsValid();
  bindingStateToken_ = nextStateToken_++;
  savedBindingStateToken_ = saved ? bindingStateToken_ : 0u;
  ClearHistory();
}

void ModelMaterialWorkspaceDocument::OpenMaterial(
    const AssetGuid &assetGuid, std::filesystem::path sourcePath,
    MaterialAssetData source, const MaterialCompileResult &compile,
    const MaterialProgramIR *lastGoodProgram, bool saved) {
  materialAssetGuid_ = assetGuid;
  materialSourcePath_ = std::move(sourcePath);
  material_ = std::move(source);
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
  ClearHistory();
}

void ModelMaterialWorkspaceDocument::CloseMaterial() {
  materialAssetGuid_ = {};
  materialSourcePath_.clear();
  material_ = {};
  compile_ = {};
  lastGoodProgram_.reset();
  abBefore_.reset();
  materialOpen_ = false;
  materialRevision_ = 0u;
  compileRevision_ = 0u;
  materialStateToken_ = 0u;
  savedMaterialStateToken_ = 0u;
  ClearHistory();
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
  return historyCursor_ > 0u && !abBefore_;
}

bool ModelMaterialWorkspaceDocument::CanRedo() const noexcept {
  return historyCursor_ < history_.size() && !abBefore_;
}

bool ModelMaterialWorkspaceDocument::Undo(ChangedDomain &outDomain,
                                          std::string &outLabel) {
  if (!CanUndo()) {
    return false;
  }
  HistoryEntry &entry = history_[--historyCursor_];
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
  HistoryEntry &entry = history_[historyCursor_++];
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
  return CanUndo() ? &history_[historyCursor_ - 1u].label : nullptr;
}

const std::string *
ModelMaterialWorkspaceDocument::GetRedoLabel() const noexcept {
  return CanRedo() ? &history_[historyCursor_].label : nullptr;
}

bool ModelMaterialWorkspaceDocument::IsBindingDirty() const noexcept {
  return bindingsOpen_ && bindingStateToken_ != savedBindingStateToken_;
}

bool ModelMaterialWorkspaceDocument::IsMaterialDirty() const noexcept {
  return materialOpen_ && materialStateToken_ != savedMaterialStateToken_;
}

void ModelMaterialWorkspaceDocument::MarkBindingsSaved(
    const AssetGuid &assetGuid, std::filesystem::path sourcePath) noexcept {
  bindingAssetGuid_ = assetGuid;
  bindingSourcePath_ = std::move(sourcePath);
  savedBindingStateToken_ = bindingStateToken_;
}

void ModelMaterialWorkspaceDocument::MarkMaterialSaved(
    const AssetGuid &assetGuid, std::filesystem::path sourcePath) noexcept {
  materialAssetGuid_ = assetGuid;
  materialSourcePath_ = std::move(sourcePath);
  savedMaterialStateToken_ = materialStateToken_;
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

void ModelMaterialWorkspaceDocument::TruncateRedo() {
  if (historyCursor_ < history_.size()) {
    history_.erase(history_.begin() +
                       static_cast<std::ptrdiff_t>(historyCursor_),
                   history_.end());
  }
}

void ModelMaterialWorkspaceDocument::PushHistory(HistoryEntry entry) {
  TruncateRedo();
  history_.push_back(std::move(entry));
  historyCursor_ = history_.size();
  if (history_.size() <= kMaximumHistoryEntries) {
    return;
  }
  history_.erase(history_.begin());
  --historyCursor_;
}

void ModelMaterialWorkspaceDocument::RefreshCompile() {
  compile_ = CompileMaterialAsset(material_, GetLastGoodProgram());
  if (compile_.success) {
    lastGoodProgram_ = compile_.program;
  }
  compileRevision_ = materialRevision_;
}

void ModelMaterialWorkspaceDocument::ClearHistory() noexcept {
  history_.clear();
  historyCursor_ = 0u;
}

} // namespace HIKARI::EDITOR
