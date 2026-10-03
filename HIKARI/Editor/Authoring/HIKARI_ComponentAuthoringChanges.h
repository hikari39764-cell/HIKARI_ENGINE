#pragma once

#include <json.hpp>

namespace HIKARI::EDITOR {

// Deserialize/Serialize の正規化ではなく、操作前後の差だけを文書へ反映する。
inline void ApplyComponentAuthoringChanges(nlohmann::json& document, const nlohmann::json& before,
                                           const nlohmann::json& after) {
    if (before == after) {
        return;
    }
    if (before.is_object() && after.is_object() && document.is_object()) {
        for (const auto& [key, value] : before.items()) {
            (void)value;
            if (!after.contains(key)) {
                document.erase(key);
            }
        }
        for (const auto& [key, value] : after.items()) {
            const auto previous = before.find(key);
            if (previous == before.end()) {
                document[key] = value;
            } else if (*previous != value) {
                if (document.contains(key)) {
                    ApplyComponentAuthoringChanges(document[key], *previous, value);
                } else {
                    document[key] = value;
                }
            }
        }
    } else if (before.is_array() && after.is_array() && document.is_array() && before.size() == after.size() &&
               document.size() == after.size()) {
        for (size_t index = 0u; index < after.size(); ++index) {
            ApplyComponentAuthoringChanges(document[index], before[index], after[index]);
        }
    } else {
        document = after;
    }
}

} // namespace HIKARI::EDITOR
