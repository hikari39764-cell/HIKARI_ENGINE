#pragma once

#include <json.hpp>
#include <cstdint>
#include <unordered_map>

namespace HIKARI {

    struct SceneObjectData;
    struct SceneComponentData;

    namespace SCENE::SERIALIZATION {

        enum class SceneObjectJsonScope { SceneDocument, PrefabRoot };
        void NormalizeLegacySceneObjectReferences(SceneComponentData& component);

        void SerializeSceneObjectJson(const SceneObjectData& object, nlohmann::json& out, SceneObjectJsonScope scope);

        bool DeserializeSceneObjectJson(const nlohmann::json& node, SceneObjectData& outObject, SceneObjectJsonScope scope);

        // Only explicitly tagged references are remapped; unrelated numeric
        // gameplay properties must never be interpreted as object identities.
        inline bool ReadSceneObjectReference(const nlohmann::json& value, uint64_t& outId) noexcept {
            const nlohmann::json* id = &value;
            if (value.is_object()) {
                const auto kind = value.find("$hikariRef");
                const auto member = value.find("id");
                if (kind == value.end() || !kind->is_string() ||
                    *kind != "SceneObject" || member == value.end()) return false;
                id = &*member;
            }
            if (id->is_number_unsigned()) {
                outId = id->get<uint64_t>();
                return true;
            }
            if (id->is_number_integer() && id->get<int64_t>() >= 0) {
                outId = static_cast<uint64_t>(id->get<int64_t>());
                return true;
            }
            return false;
        }

        inline nlohmann::json WriteSceneObjectReference(uint64_t id) {
            return {{"$hikariRef", "SceneObject"}, {"id", id}};
        }
        inline uint64_t SceneObjectReferenceOr(const nlohmann::json& value, uint64_t fallback = 0) noexcept {
            uint64_t result = fallback;
            return ReadSceneObjectReference(value, result) ? result : fallback;
        }
        uint32_t RemapSceneObjectReferences(nlohmann::json& properties,
            const std::unordered_map<uint64_t, uint64_t>& ids,
            bool clearExternalReferences = false);

    } // namespace SCENE::SERIALIZATION

} // namespace HIKARI
