#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceDocument.h"

#include "Assets/Material/Serialization/HIKARI_MaterialSourceJson.h"

#include <functional>
#include <string>
#include <utility>

#include "Assets/Material/HIKARI_MaterialAssetData.h"

namespace HIKARI::EDITOR {

bool ModelMaterialWorkspaceDocument::AreMaterialContentsEquivalent(
    const MaterialAssetData& left, const MaterialAssetData& right) {
  std::string leftText, rightText, error;
  return MATERIAL_SERIALIZATION::SerializeMaterialCompileInputJson(left, leftText, error) &&
      MATERIAL_SERIALIZATION::SerializeMaterialCompileInputJson(right, rightText, error) && leftText == rightText;
}

bool ModelMaterialWorkspaceDocument::LastMaterialChangeRequiresPreview() const noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return materialContentChanged_;
}

void ModelMaterialWorkspaceDocument::RefreshAfterMaterialChange(
    const MaterialAssetData& before) {
  const bool wasCurrent = compileRevision_ == materialRevision_;
  ++materialRevision_;
  materialContentChanged_ = !AreMaterialContentsEquivalent(before, material_);
  if (materialContentChanged_) {
    RefreshCompile();
  } else if (wasCurrent) {
    compileRevision_ = materialRevision_;
  }
}

bool ModelMaterialWorkspaceDocument::CommitMaterialLocked(
    MaterialAssetData before, std::string label) {
  if (!materialOpen_ || AreMaterialsEquivalent(before, material_)) {
    return false;
  }
  const bool contentChanged = !AreMaterialContentsEquivalent(before, material_);
  if (contentChanged) SyncPbrFromGraphParameters(material_);
  if (abBefore_) {
    RefreshAfterMaterialChange(before);
    return true;
  }
  HistoryEntry entry{};
  entry.domain = contentChanged ? ChangedDomain::Material : ChangedDomain::MaterialLayout;
  entry.beforeMaterial = std::move(before);
  entry.afterMaterial = material_;
  entry.beforeToken = materialStateToken_;
  entry.afterToken = nextStateToken_++;
  entry.label = std::move(label);
  materialStateToken_ = entry.afterToken;
  activeHistoryDomain_ = ChangedDomain::Material;
  RefreshAfterMaterialChange(entry.beforeMaterial);
  PushHistory(std::move(entry));
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
