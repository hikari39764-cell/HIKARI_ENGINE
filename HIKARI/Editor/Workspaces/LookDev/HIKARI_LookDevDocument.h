#pragma once

#include <string_view>

#include "Core/Math/HIKARI_Math3D.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {
class DocumentSceneBase;
}

// 布景の操作が文書の物体を書き換える時の小さな共通処理。
namespace HIKARI::EDITOR {

SceneComponentData* FindLookDevComponent(SceneObjectData& object, std::string_view type) noexcept;

// 文書の物体を実行時の物体へ写す（局所の変形と、同じ種類の順番で対応させた部品の作者値）。ドラッグ中の見た目に使う。
void SyncLookDevRuntimeObject(DocumentSceneBase& scene, const SceneObjectData& object);

// 世界の移動量を、物体の親の局所の移動量へ（親が無ければそのまま）。文書の位置は親の局所。
MATH::Vec3 LookDevParentLocalVector(DocumentSceneBase& scene, const SceneObjectData& object, const MATH::Vec3& world);

// 実行時の物体の世界の行列（無ければ単位行列）。
MATH::Mat4 LookDevWorldMatrix(DocumentSceneBase& scene, SceneObjectId id);

} // namespace HIKARI::EDITOR
