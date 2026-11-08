#include "Editor/Authoring/HIKARI_EditorObjectState.h"

#include <unordered_set>

#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::EDITOR {

bool IsObjectEditorLocked(const SceneDocument& document, SceneObjectId objectId) noexcept {
    if (objectId.value == 0u) {
        return false;
    }
    for (const SceneObjectData& object : document.objects) {
        if (object.id == objectId) {
            return object.editorLocked;
        }
    }
    return false;
}

bool HasSelectedObjectWithFlag(const SceneDocument& document, const std::vector<SceneObjectId>& objectIds,
                               SceneObjectFlag flag, bool value) noexcept {
    std::unordered_set<uint64_t> selectedIds{};
    selectedIds.reserve(objectIds.size());
    for (SceneObjectId objectId : objectIds) {
        if (objectId.value != 0u) {
            selectedIds.insert(objectId.value);
        }
    }
    for (const SceneObjectData& object : document.objects) {
        if (selectedIds.contains(object.id.value) && object.*flag == value) {
            return true;
        }
    }
    return false;
}

std::size_t SetObjectsFlag(SceneDocument& document, const std::vector<SceneObjectId>& objectIds, SceneObjectFlag flag,
                           bool value) {
    std::unordered_set<uint64_t> selectedIds{};
    selectedIds.reserve(objectIds.size());
    for (SceneObjectId objectId : objectIds) {
        if (objectId.value != 0u) {
            selectedIds.insert(objectId.value);
        }
    }

    std::size_t changedCount = 0u;
    for (SceneObjectData& object : document.objects) {
        if (!selectedIds.contains(object.id.value) || object.*flag == value) {
            continue;
        }
        object.*flag = value;
        ++changedCount;
    }
    return changedCount;
}

std::unordered_set<uint64_t> CollectEditorLockedObjectIds(const SceneDocument& document) {
    std::unordered_set<uint64_t> lockedIds{};
    for (const SceneObjectData& object : document.objects) {
        if (object.editorLocked) {
            lockedIds.insert(object.id.value);
        }
    }
    return lockedIds;
}

} // namespace HIKARI::EDITOR
