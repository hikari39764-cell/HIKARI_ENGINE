#pragma once

namespace HIKARI {

class AssetRegistry;

class ModelComponent;

namespace SAND {

// TerrainSurface の組み込み layer-atlas マテリアルを slot 0 へ適用する。
// すでに同じ atlas を指す場合は何もしない。
// ランタイム World の再構築タイミングに依存しないよう、
// SceneRuntimeBuilder と SceneRenderCacheSync の両方から呼ぶ。
bool EnsureSandSurfaceMaterialOverride(ModelComponent& model, const AssetRegistry& assetRegistry) noexcept;

} // namespace SAND

} // namespace HIKARI
