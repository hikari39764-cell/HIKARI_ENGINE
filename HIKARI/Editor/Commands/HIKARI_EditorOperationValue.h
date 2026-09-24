#pragma once
#include <string>
#include <json.hpp>
namespace HIKARI::EDITOR {
inline float ReadEditorOperationFloat(const nlohmann::json& json, const char* key, float fallback) {
    const auto found = json.find(key);
    if (found == json.end()) {
        return fallback;
    }
    if (found->is_number()) {
        return found->get<float>();
    }
    if (found->is_string()) {
        try {
            return std::stof(found->get<std::string>());
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}
} // namespace HIKARI::EDITOR
