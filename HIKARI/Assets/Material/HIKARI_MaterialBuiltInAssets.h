#pragma once

#include <string_view>

namespace HIKARI::ASSETS::MATERIAL {

// プロシージャルグラフィックス用マテリアルスロットの内置「デフォルト白」ID。
// AssetRegistryBuilder がランタイム MaterialAssetDescriptor として登録し、
// シーンシリアライズ時はこの ID を直接保存する（ディスク資産不要）。
inline constexpr std::string_view kBuiltInWhiteMaterialAssetId =
    "generated://solid/ffffffff";

// プロシージャルグラフィックス用の内置「チェッカー(デバッグ)」ID。
inline constexpr std::string_view kBuiltInCheckerMaterialAssetId =
    "generated://checker/bfbfbfff/8a8a8aff";

constexpr bool IsBuiltInWhiteMaterialAssetId(
    std::string_view id) noexcept {
    return id == kBuiltInWhiteMaterialAssetId;
}

constexpr bool IsBuiltInCheckerMaterialAssetId(
    std::string_view id) noexcept {
    return id == kBuiltInCheckerMaterialAssetId;
}

// プロシージャル砂地の既定マテリアル。SceneRuntimeBuilder が
// SandSurfaceComponent を持つオブジェクトの slot 0 へ上書きする。
inline constexpr std::string_view kBuiltInSandMaterialAssetId =
    "generated://sand/anime-dry";

// HIKARI/EngineContent/Sand/BuiltinAssets.json が登録する固定アセット ID。
// CC0 PBR セット（ambientCG Ground054）由来。アルベドは Color、
// 法線は DirectX 慣習（緑 -Y）の NormalDX を使用する。
inline constexpr std::string_view kBuiltInSandAlbedoTextureId =
    "engine.sand.texture.ground054_albedo";

inline constexpr std::string_view kBuiltInSandNormalTextureId =
    "engine.sand.texture.ground054_normal";

constexpr bool IsBuiltInSandMaterialAssetId(
    std::string_view id) noexcept {
    return id == kBuiltInSandMaterialAssetId;
}

} // namespace HIKARI::ASSETS::MATERIAL
