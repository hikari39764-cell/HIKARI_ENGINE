#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

struct SceneDocument;
struct SceneObjectData;

namespace EDITOR {

// 物体の真偽の印（編集器のロック、分区場景の常駐など）。
using SceneObjectFlag = bool SceneObjectData::*;

bool IsObjectEditorLocked(const SceneDocument& document, SceneObjectId objectId) noexcept;

bool HasSelectedObjectWithFlag(const SceneDocument& document, const std::vector<SceneObjectId>& objectIds,
                               SceneObjectFlag flag, bool value) noexcept;

std::size_t SetObjectsFlag(SceneDocument& document, const std::vector<SceneObjectId>& objectIds, SceneObjectFlag flag,
                           bool value);

std::unordered_set<uint64_t> CollectEditorLockedObjectIds(const SceneDocument& document);

} // namespace EDITOR
} // namespace HIKARI
