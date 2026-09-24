#pragma once

#include "Scene/HIKARI_SceneObjectId.h"
#include <span>
#include <unordered_map>
#include <unordered_set>

namespace HIKARI {

struct SceneDocument;
struct SceneObjectData;

SceneObjectId AllocateSceneObjectId(const SceneDocument& document);
SceneObjectData* FindSceneObjectData(SceneDocument& document, SceneObjectId id) noexcept;
const SceneObjectData* FindSceneObjectData(const SceneDocument& document, SceneObjectId id) noexcept;
std::unordered_set<uint64_t> CollectObjectSubtreeIds(const SceneDocument& document, SceneObjectId rootId);
// Stage the complete clone before publishing it. Parent links and explicit
// property references use the same ID map. Existing IDs are never reused.
bool AppendClonedSceneObjects(std::span<const SceneObjectData> source, SceneDocument& destination, uint64_t& nextId,
                              std::unordered_map<uint64_t, uint64_t>& outIds, bool detachExternalParents = false);
// デフォルトシーンに置く実体のグリッド床オブジェクトを組み立てる。
// チェッカー既定マテリアルを持つ GridPlane 100x100 (50x50 セグメント)。
SceneObjectData BuildDefaultGridFloorSceneObject(SceneDocument& document);

} // namespace HIKARI
