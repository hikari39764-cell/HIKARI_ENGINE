#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

    struct SceneDocument;

    namespace EDITOR {

        bool IsObjectEditorLocked(
            const SceneDocument& document,
            SceneObjectId objectId) noexcept;

        bool HasSelectedObjectWithEditorLock(
            const SceneDocument& document,
            const std::vector<SceneObjectId>& objectIds,
            bool locked) noexcept;

        std::size_t SetObjectsEditorLocked(
            SceneDocument& document,
            const std::vector<SceneObjectId>& objectIds,
            bool locked);

        std::unordered_set<uint64_t> CollectEditorLockedObjectIds(
            const SceneDocument& document);

    } // namespace EDITOR
} // namespace HIKARI
