#pragma once

#include <string>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"

namespace HIKARI {

class AssetDatabase;
class AssetRegistry;
struct EditorSelection;

class AssetInspectorPanel {
public:
  void Draw(AssetDatabase &assetDatabase, AssetRegistry &assetRegistry,
            EditorSelection &selection) const;
  bool ConsumeApplyRuntimeMaterialRequest(AssetGuid &outGuid,
                                          PbrMaterialAssetData &outData) const;
  std::string ConsumeRefreshRuntimeMaterialGuid() const;

private:
  mutable AssetGuid applyRuntimeMaterialGuid_{};
  mutable PbrMaterialAssetData applyRuntimeMaterialData_{};
  mutable std::string refreshRuntimeMaterialGuid_{};
  mutable std::string pendingRefreshRuntimeMaterialGuid_{};
};

} // namespace HIKARI
