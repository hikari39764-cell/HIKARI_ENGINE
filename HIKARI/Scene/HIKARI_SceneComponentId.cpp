#include "Scene/HIKARI_SceneComponentId.h"
#include "Core/Numeric/HIKARI_NonZero.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

namespace {
SceneComponentId FirstAvailableComponentId(const std::unordered_set<uint64_t>& usedIds, uint64_t preferred) {
    uint64_t candidate = preferred == 0u ? 1u : preferred;
    while (candidate == 0u || usedIds.contains(candidate)) {
        NUMERIC::AdvanceNonZero(candidate);
    }
    return SceneComponentId{candidate};
}
} // namespace

SceneComponentId AllocateSceneComponentId(const SceneObjectData& object) {
    std::unordered_set<uint64_t> usedIds{};
    uint64_t maximumId = 0u;
    for (const SceneComponentData& component : object.components) {
        if (component.id.value == 0u) {
            continue;
        }
        usedIds.insert(component.id.value);
        maximumId = (std::max)(maximumId, component.id.value);
    }
    const uint64_t preferred = maximumId == (std::numeric_limits<uint64_t>::max)() ? 1u : maximumId + 1u;
    return FirstAvailableComponentId(usedIds, preferred);
}

void NormalizeSceneComponentIds(SceneObjectData& object) {
    std::unordered_set<uint64_t> usedIds{};
    uint64_t nextId = 1u;
    for (SceneComponentData& component : object.components) {
        if (component.id.value != 0u && usedIds.insert(component.id.value).second) {
            if (component.id.value >= nextId && component.id.value != (std::numeric_limits<uint64_t>::max)()) {
                nextId = component.id.value + 1u;
            }
            continue;
        }
        const SceneComponentId allocated = FirstAvailableComponentId(usedIds, nextId);
        component.id = allocated;
        usedIds.insert(allocated.value);
        nextId = allocated.value == (std::numeric_limits<uint64_t>::max)() ? 1u : allocated.value + 1u;
    }
}

std::optional<size_t> FindSceneComponentIndex(const SceneObjectData& object, SceneComponentId id) noexcept {
    if (id.value == 0u) {
        return std::nullopt;
    }
    for (size_t index = 0u; index < object.components.size(); ++index) {
        if (object.components[index].id == id) {
            return index;
        }
    }
    return std::nullopt;
}

SceneComponentData* FindSceneComponent(SceneObjectData& object, SceneComponentId id) noexcept {
    const std::optional<size_t> index = FindSceneComponentIndex(object, id);
    return index.has_value() ? &object.components[*index] : nullptr;
}

const SceneComponentData* FindSceneComponent(const SceneObjectData& object, SceneComponentId id) noexcept {
    const std::optional<size_t> index = FindSceneComponentIndex(object, id);
    return index.has_value() ? &object.components[*index] : nullptr;
}

} // namespace HIKARI
