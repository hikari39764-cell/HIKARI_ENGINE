#pragma once

#include <json.hpp>
#include <cstdint>
#include <unordered_map>

namespace HIKARI {

struct SceneObjectData;
struct SceneComponentData;

namespace SCENE::SERIALIZATION {

enum class SceneObjectJsonScope { SceneDocument, PrefabRoot };

void SerializeSceneObjectJson(const SceneObjectData& object, nlohmann::json& out, SceneObjectJsonScope scope);

bool DeserializeSceneObjectJson(const nlohmann::json& node, SceneObjectData& outObject, SceneObjectJsonScope scope);

// Only explicitly tagged references are remapped; unrelated numeric
// gameplay properties must never be interpreted as object identities.
inline bool ReadSceneObjectReference(const nlohmann::json& value, uint64_t& outId) noexcept {
    if (!value.is_object()) {
        return false;
    }
    const auto kind = value.find("$hikariRef");
    const auto id = value.find("id");
    if (kind == value.end() || !kind->is_string() || *kind != "SceneObject" || id == value.end() ||
        !id->is_number_unsigned()) {
        return false;
    }
    outId = id->get<uint64_t>();
    return true;
}

inline nlohmann::json WriteSceneObjectReference(uint64_t id) { return {{"$hikariRef", "SceneObject"}, {"id", id}}; }
inline uint64_t SceneObjectReferenceOr(const nlohmann::json& value, uint64_t fallback = 0) noexcept {
    uint64_t result = fallback;
    return ReadSceneObjectReference(value, result) ? result : fallback;
}
uint32_t RemapSceneObjectReferences(nlohmann::json& properties, const std::unordered_map<uint64_t, uint64_t>& ids,
                                    bool clearExternalReferences = false);

} // namespace SCENE::SERIALIZATION

} // namespace HIKARI
