#include "Editor/Authoring/HIKARI_SceneTransformAuthoring.h"

#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace HIKARI::EDITOR {
    namespace {
        nlohmann::json Vector(const MATH::Vec3& value) { return {value.x, value.y, value.z}; }
        bool ReadVector(const nlohmann::json& object, const char* key, MATH::Vec3& output, bool scale) {
            const auto found = object.find(key);
            if (found == object.end()) return true;
            if (!found->is_array() || found->size() != 3) return false;
            float values[3];
            for (size_t i = 0; i < 3; ++i) {
                if (!(*found)[i].is_number()) return false;
                values[i] = (*found)[i].get<float>();
                if (!std::isfinite(values[i]) || std::abs(values[i]) > 1000000.0f ||
                    (scale && std::abs(values[i]) < 0.0001f)) return false;
            }
            output = {values[0], values[1], values[2]};
            return true;
        }
    }

    nlohmann::json DescribeSceneObjectTransform(const SceneObjectData& object) {
        return {{"position", Vector(object.transform.position)},
            {"rotation", Vector(object.transform.rotationEulerDeg)},
            {"scale", Vector(object.transform.scale)},
            {"parentId", object.parent ? object.parent->value : 0ull}};
    }

    EditorOperationResult StageSceneTransformEdits(const SceneDocument& document,
        const nlohmann::json& commands, std::vector<SceneObjectData>& output) {
        if (!commands.is_array() || commands.empty() || commands.size() > 256) {
            return {false, "INVALID_COMMANDS", "A transform transaction needs 1 to 256 commands."};
        }
        std::unordered_map<uint64_t, size_t> indices;
        for (size_t i = 0; i < document.objects.size(); ++i) {
            if (!indices.emplace(document.objects[i].id.value, i).second) {
                return {false, "DUPLICATE_OBJECT_ID", "Repair duplicate object identities before editing."};
            }
        }
        auto staged = document.objects;
        std::unordered_set<uint64_t> edited;
        bool changed = false;
        for (const auto& command : commands) {
            if (!command.is_object() || !command.contains("objectId") ||
                !command["objectId"].is_number_unsigned()) {
                // JSON parsers may retain a positive signed integer in native callers.
                if (!command.is_object() || !command.contains("objectId") ||
                    !command["objectId"].is_number_integer() || command["objectId"].get<int64_t>() <= 0)
                    return {false, "INVALID_OBJECT_ID", "Each command needs a positive objectId."};
            }
            const auto id = command["objectId"].get<uint64_t>();
            const auto found = indices.find(id);
            if (id == 0 || found == indices.end()) return {false, "OBJECT_NOT_FOUND", "Missing object " + std::to_string(id)};
            if (!edited.insert(id).second) return {false, "DUPLICATE_COMMAND", "One transform command per object is allowed."};
            auto& object = staged[found->second];
            if (object.editorLocked) return {false, "OBJECT_LOCKED", "Unlock object " + std::to_string(id) + " before editing."};
            const auto before = DescribeSceneObjectTransform(object);
            if (command.contains("expected") && command["expected"] != before) {
                return {false, "DOCUMENT_CONFLICT", "Object " + std::to_string(id) + " changed. Refresh the draft before applying."};
            }
            if (!ReadVector(command, "position", object.transform.position, false) ||
                !ReadVector(command, "rotation", object.transform.rotationEulerDeg, false) ||
                !ReadVector(command, "scale", object.transform.scale, true)) {
                return {false, "INVALID_TRANSFORM", "Transforms need finite XYZ values; zero scale is not supported."};
            }
            changed |= before != DescribeSceneObjectTransform(object);
        }
        output = std::move(staged);
        return {true, changed ? "OK" : "NO_CHANGE", changed ? "Transforms staged." : "No transform changed."};
    }
}
