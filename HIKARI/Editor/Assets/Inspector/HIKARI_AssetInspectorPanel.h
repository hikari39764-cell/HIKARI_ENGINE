#pragma once

#include <array>
#include <chrono>
#include <string>
#include <unordered_map>
#include <json.hpp>
#include "Assets/HIKARI_AssetDatabase.h"

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Assets/HIKARI_AssetFileCache.h"
#include "Assets/Sprite/HIKARI_SpriteMaterialAsset.h"
#include "Editor/Assets/HIKARI_AssetImportSettingsService.h"

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
    void DrawSpriteMaterialEditor(AssetDatabase& database, AssetRegistry& registry, const AssetRecord& record) const;
    bool DrawSpriteShaderCreation(AssetDatabase& database, AssetRegistry& registry, const AssetRecord& record,
                                  EditorSelection& selection) const;
    mutable std::filesystem::path spriteMaterialProject_;
    mutable std::string spriteMaterialGuid_, spriteMaterialStatus_;
    mutable uint64_t spriteMaterialRevision_ = 0u;
    mutable SPRITE::SpriteMaterialAssetData spriteMaterialEdit_;
    mutable bool spriteMaterialLoaded_ = false, spriteMaterialDirty_ = false;
    mutable AssetFileCache<SPRITE::SpriteMaterialAssetData, AssetType::SpriteMaterial,
                           &SPRITE::LoadSpriteMaterialAssetData>
        spriteMaterialCache_;
    mutable std::shared_ptr<const ASSETS::SHADER::ShaderCookedArtifact> spriteShader_;
    mutable std::string spriteShaderDiagnostic_;
    mutable bool spriteShaderValid_ = false;
    bool ResolveSpriteInspectorShader(AssetRegistry& registry, const AssetGuid& guid) const;
    void RefreshFilePreview(const AssetDatabase& database, const AssetRecord& record) const;
    mutable std::filesystem::path loadedProject_{};
    mutable std::string loadedGuid_{};
    mutable std::unordered_map<std::string, EDITOR::AssetImportSettingsDraft> importDrafts_{};
    mutable std::string importSettingsStatus_{};
    mutable AssetMeta importDefaults_{};
    mutable std::array<char, 256> displayNameBuffer_{};
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
