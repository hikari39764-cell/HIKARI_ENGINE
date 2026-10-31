#pragma once

#include <json.hpp>
#include <cstdint>
#include <unordered_map>

#include "Scene/HIKARI_SceneObjectId.h"

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

// SceneObjectId を項目表（JSONFIELDS の ObjectRef）などから nlohmann で直接読み書きできるようにする。
// 保存形は上の物体参照で、形が違えば例外を投げる。参照なしは数値の 0 でも読む（以前の既定値が書いた文書がある）。
// 0 以外の数値は物体の ID として読まない。
inline void to_json(nlohmann::json& json, const SceneObjectId& id) {
    json = SCENE::SERIALIZATION::WriteSceneObjectReference(id.value);
}

inline void from_json(const nlohmann::json& json, SceneObjectId& id) {
    if (json.is_number() && json.get<double>() == 0.0) {
        id.value = 0;
    } else if (!SCENE::SERIALIZATION::ReadSceneObjectReference(json, id.value)) {
        throw nlohmann::json::type_error::create(302, "type must be a scene object reference", &json);
    }
}

} // namespace HIKARI
