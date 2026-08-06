#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceDocument.h"

#include <functional>
#include <string>
#include <utility>

#include "Assets/Material/HIKARI_MaterialAssetData.h"

namespace HIKARI::EDITOR {

bool ModelMaterialWorkspaceDocument::CommitMaterialLocked(
    MaterialAssetData before, std::string label) {
  if (!materialOpen_ || AreMaterialsEquivalent(before, material_)) {
    return false;
  }
  SyncPbrFromGraphParameters(material_);
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

bool ModelMaterialWorkspaceDocument::ApplyMaterialEdit(
    const std::function<void(MaterialAssetData &)> &edit,
    std::string label,
    std::string &outMessage) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!materialOpen_) {
    outMessage = "未打开材质文档";
    return false;
  }
  MaterialAssetData before = material_;
  edit(material_);
  if (AreMaterialsEquivalent(before, material_)) {
    outMessage = "材质没有变化";
    return false;
  }
  outMessage.clear();
  return CommitMaterialLocked(std::move(before), std::move(label));
}

} // namespace HIKARI::EDITOR
