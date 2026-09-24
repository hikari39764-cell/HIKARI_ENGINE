#pragma once

#include <array>
#include <chrono>
#include <string>
#include <json.hpp>
#include "Assets/HIKARI_AssetDatabase.h"

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"

namespace HIKARI {

class AssetDatabase;
class AssetRegistry;
struct EditorSelection;

class AssetInspectorPanel {
public:
    void Draw(AssetDatabase& assetDatabase, AssetRegistry& assetRegistry, EditorSelection& selection) const;
    bool ConsumeApplyRuntimeMaterialRequest(AssetGuid& outGuid, PbrMaterialAssetData& outData) const;
    std::string ConsumeRefreshRuntimeMaterialGuid() const;

private:
    void RefreshFilePreview(const AssetDatabase& database, const AssetRecord& record) const;
    mutable std::filesystem::path loadedProject_{};
    mutable std::string loadedGuid_{};
    mutable std::array<char, 256> displayNameBuffer_{};
    mutable std::array<char, 16384> importSettingsBuffer_{};
    mutable std::string parsedSettingsSource_{};
    mutable nlohmann::json parsedSettings_{};
    mutable std::string filePreviewKey_{};
    mutable uint64_t filePreviewRevision_ = 0;
    mutable std::chrono::steady_clock::time_point nextFilePreview_{};
    mutable ClusteredGeometryArtifactInfo modelInfo_{};
    mutable nlohmann::json modelReport_{};
    mutable std::string rawMeta_{};
    mutable std::string rawReport_{};
    mutable AssetGuid applyRuntimeMaterialGuid_{};
    mutable PbrMaterialAssetData applyRuntimeMaterialData_{};
    mutable std::string refreshRuntimeMaterialGuid_{};
    mutable std::string pendingRefreshRuntimeMaterialGuid_{};
};

} // namespace HIKARI
