#pragma once

namespace HIKARI {

    class AssetRegistry;

    class ModelComponent;

    namespace SAND {

        // 沙地の組み込みマテリアルを slot 0 へ適用する。すでに適用済み
        // （ベースカラーテクスチャが Sand アルベドを指す）なら何もしない。
        // ランタイム World の再構築タイミングに依存しないよう、
        // SceneRuntimeBuilder と SceneRenderCacheSync の両方から呼ぶ。
        bool EnsureSandSurfaceMaterialOverride(
            ModelComponent& model,
            const AssetRegistry& assetRegistry) noexcept;

    } // namespace SAND

} // namespace HIKARI
