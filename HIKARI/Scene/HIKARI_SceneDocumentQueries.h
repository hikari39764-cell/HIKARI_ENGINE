#pragma once

#include "Scene/HIKARI_SceneObjectId.h"
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace HIKARI {

struct SceneDocument;
struct SceneObjectData;

SceneObjectId AllocateSceneObjectId(const SceneDocument& document);
// 玩家：角色入力を持つ物、無ければ角色の移動を持つ物。見つからなければ無効な ID。
SceneObjectId FindScenePlayer(const SceneDocument& document) noexcept;
SceneObjectData* FindSceneObjectData(SceneDocument& document, SceneObjectId id) noexcept;
const SceneObjectData* FindSceneObjectData(const SceneDocument& document, SceneObjectId id) noexcept;
std::unordered_set<uint64_t> CollectObjectSubtreeIds(const SceneDocument& document, SceneObjectId rootId);
// ids のうち、祖先が ids に含まれない物（選択の一番上の物）。順番は ids のまま。
std::vector<SceneObjectId> CollectRootObjectIds(const SceneDocument& document, std::span<const SceneObjectId> ids);
struct SceneObjectChanges;
// before から after への追加・削除・変更（追加と変更は after 順、削除は before 順）。
SceneObjectChanges DiffSceneObjects(std::span<const SceneObjectData> before, std::span<const SceneObjectData> after);
// Stage the complete clone before publishing it. Parent links and explicit
// property references use the same ID map. Existing IDs are never reused.
bool AppendClonedSceneObjects(std::span<const SceneObjectData> source, SceneDocument& destination, uint64_t& nextId,
                              std::unordered_map<uint64_t, uint64_t>& outIds, bool detachExternalParents = false);
// デフォルトシーンに置く実体のグリッド床オブジェクトを組み立てる。
// チェッカー既定マテリアルを持つ GridPlane 100x100 (50x50 セグメント)。
SceneObjectData BuildDefaultGridFloorSceneObject(SceneDocument& document);

} // namespace HIKARI
