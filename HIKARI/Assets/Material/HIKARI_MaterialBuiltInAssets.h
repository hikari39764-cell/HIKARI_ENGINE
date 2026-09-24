#pragma once

#include <string_view>

namespace HIKARI::ASSETS::MATERIAL {

// プロシージャルグラフィックス用マテリアルスロットの内置「デフォルト白」ID。
// AssetRegistryBuilder がランタイム MaterialAssetDescriptor として登録し、
// シーンシリアライズ時はこの ID を直接保存する（ディスク資産不要）。
inline constexpr std::string_view kBuiltInWhiteMaterialAssetId = "generated://solid/ffffffff";

// プロシージャルグラフィックス用の内置「チェッカー(デバッグ)」ID。
inline constexpr std::string_view kBuiltInCheckerMaterialAssetId = "generated://checker/bfbfbfff/8a8a8aff";

// 旧プロシージャル選択名は、依存解決と実体生成で同じ ID に移行する。
constexpr std::string_view CanonicalizeProceduralMaterialSelection(std::string_view selection) noexcept {
    return selection == "checker" ? kBuiltInCheckerMaterialAssetId : selection;
}

constexpr bool IsBuiltInWhiteMaterialAssetId(std::string_view id) noexcept {
    return id == kBuiltInWhiteMaterialAssetId;
}

constexpr bool IsBuiltInCheckerMaterialAssetId(std::string_view id) noexcept {
    return id == kBuiltInCheckerMaterialAssetId;
}

// プロシージャル砂地の既定マテリアル。SceneRuntimeBuilder が
// TerrainSurfaceComponent を持つオブジェクトの slot 0 へ上書きする。
inline constexpr std::string_view kBuiltInSandMaterialAssetId = "generated://sand/anime-dry";

// HIKARI/EngineContent/Sand/BuiltinAssets.json が登録する固定アセット ID。
// CC0 PBR セット由来。アルベドは ambientCG Ground101（明るく平滑な砂）、
// 法線は Ground054 の DirectX 慣習（緑 -Y）NormalDX を使用する。
inline constexpr std::string_view kBuiltInSandAlbedoTextureId = "engine.sand.texture.ground101_albedo";

inline constexpr std::string_view kBuiltInSandNormalTextureId = "engine.sand.texture.ground054_normal";

// TerrainSurface の stylized layer atlas。2x2 の固定 layout は
// EngineContent/Terrain/README.md と SurfacePreset の atlas tile を共有する。
inline constexpr std::string_view kBuiltInTerrainLayerAlbedoTextureId = "engine.terrain.texture.layer_atlas_albedo";

inline constexpr std::string_view kBuiltInTerrainLayerNormalTextureId = "engine.terrain.texture.layer_atlas_normal";

constexpr bool IsBuiltInSandMaterialAssetId(std::string_view id) noexcept { return id == kBuiltInSandMaterialAssetId; }

} // namespace HIKARI::ASSETS::MATERIAL
