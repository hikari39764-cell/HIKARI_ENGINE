#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace HIKARI {

struct SceneComponentData;
struct SceneObjectData;

struct SceneComponentId {
    uint64_t value = 0;

    bool operator==(const SceneComponentId& rhs) const noexcept { return value == rhs.value; }
};

SceneComponentId AllocateSceneComponentId(const SceneObjectData& object);
void NormalizeSceneComponentIds(SceneObjectData& object);
std::optional<size_t> FindSceneComponentIndex(const SceneObjectData& object, SceneComponentId id) noexcept;
SceneComponentData* FindSceneComponent(SceneObjectData& object, SceneComponentId id) noexcept;
const SceneComponentData* FindSceneComponent(const SceneObjectData& object, SceneComponentId id) noexcept;

} // namespace HIKARI
