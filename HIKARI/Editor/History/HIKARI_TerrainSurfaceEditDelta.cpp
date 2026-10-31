#include "Editor/History/HIKARI_TerrainSurfaceEditDelta.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>

namespace HIKARI::EDITOR {

bool ApplyTerrainSurfaceEditDelta(nlohmann::json& componentProperties, const nlohmann::json& delta) {
    if (!delta.is_object()) {
        return false;
    }
    if (!componentProperties.is_object()) {
        componentProperties = nlohmann::json::object();
    }

    bool changed = false;
    if (const auto preset = delta.find("presetId");
        preset != delta.end() && preset->is_string() &&
        componentProperties.value("presetId", std::string{}) != preset->get<std::string>()) {
        componentProperties["presetId"] = *preset;
        changed = true;
    }
    if (const auto layerMapping = delta.find("layerPresetIds");
        layerMapping != delta.end() && layerMapping->is_array() &&
        componentProperties.value("layerPresetIds", nlohmann::json::array()) != *layerMapping) {
        componentProperties["layerPresetIds"] = *layerMapping;
        changed = true;
    }

    const auto edits = delta.find("chunks");
    if (edits == delta.end() || !edits->is_array()) {
        return changed;
    }

    using ChunkKey = std::pair<int32_t, int32_t>;
    std::map<ChunkKey, nlohmann::json> chunks{};
    if (const auto current = componentProperties.find("chunks");
        current != componentProperties.end() && current->is_array()) {
        for (const nlohmann::json& chunk : *current) {
            if (!chunk.is_object() || !chunk.contains("x") || !chunk.contains("z") || !chunk["x"].is_number_integer() ||
                !chunk["z"].is_number_integer()) {
                continue;
            }
            chunks[{chunk["x"].get<int32_t>(), chunk["z"].get<int32_t>()}] = chunk;
        }
    }

    for (const nlohmann::json& edit : *edits) {
        if (!edit.is_object() || !edit.contains("x") || !edit.contains("z") || !edit.contains("exists") ||
            !edit["x"].is_number_integer() || !edit["z"].is_number_integer() || !edit["exists"].is_boolean()) {
            continue;
        }
        const ChunkKey key{edit["x"].get<int32_t>(), edit["z"].get<int32_t>()};
        if (!edit["exists"].get<bool>()) {
            changed = chunks.erase(key) != 0u || changed;
            continue;
        }

        nlohmann::json replacement = edit;
        replacement.erase("exists");
        const auto previous = chunks.find(key);
        if (previous == chunks.end() || previous->second != replacement) {
            chunks[key] = std::move(replacement);
            changed = true;
        }
    }

    nlohmann::json rebuilt = nlohmann::json::array();
    for (const auto& [key, chunk] : chunks) {
        (void)key;
        rebuilt.push_back(chunk);
    }
    componentProperties["chunks"] = std::move(rebuilt);
    return changed;
}

} // namespace HIKARI::EDITOR
