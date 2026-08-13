#pragma once

#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

    struct SceneDocument;
    struct SceneObjectData;

    SceneObjectId AllocateSceneObjectId(
        const SceneDocument& document);
    SceneObjectData* FindSceneObjectData(
        SceneDocument& document,
        SceneObjectId id) noexcept;
    const SceneObjectData* FindSceneObjectData(
        const SceneDocument& document,
        SceneObjectId id) noexcept;
    // デフォルトシーンに置く実体のグリッド床オブジェクトを組み立てる。
    // チェッカー既定マテリアルを持つ GridPlane 100x100 (50x50 セグメント)。
    SceneObjectData BuildDefaultGridFloorSceneObject(
        SceneDocument& document);

} // namespace HIKARI
