#include "Scene/Environment/HIKARI_SceneEnvironmentBinding.h"

#include <utility>

namespace HIKARI::ENVIRONMENT {
namespace {
const nlohmann::json& ObjectOrEmpty(const nlohmann::json& node, const char* key) {
    static const nlohmann::json empty = nlohmann::json::object();
    const auto member = node.find(key);
    return member != node.end() && member->is_object() ? *member : empty;
}
} // namespace

void ReadSceneEnvironmentBinding(const nlohmann::json& node, SceneEnvironmentBinding& binding) {
    SceneEnvironmentBinding result{};
    if (node.is_object()) {
        JSONFIELDS::ReadLenient(node, result, kBindingFields);
        JSONFIELDS::ReadLenient(ObjectOrEmpty(node, "location"), result.location, kLocationFields);
        JSONFIELDS::ReadLenient(ObjectOrEmpty(node, "time"), result.time, kTimeFields);
    }
    result = JSONFIELDS::Sanitize(result, kBindingFields);
    result.location = JSONFIELDS::Sanitize(result.location, kLocationFields);
    result.time = JSONFIELDS::Sanitize(result.time, kTimeFields);
    if (!IsValidCalendarTime(result.time)) {
        result.time = CalendarTime{};
    }
    if (result.profileId.empty()) {
        result.profileId = SceneEnvironmentBinding{}.profileId;
    }
    if (result.renderStyleId.empty()) {
        result.renderStyleId = SceneEnvironmentBinding{}.renderStyleId;
    }
    binding = std::move(result);
}

nlohmann::json WriteSceneEnvironmentBinding(const SceneEnvironmentBinding& binding) {
    nlohmann::json node = nlohmann::json::object();
    JSONFIELDS::Write(node, binding, kBindingFields);
    nlohmann::json location = nlohmann::json::object();
    JSONFIELDS::Write(location, binding.location, kLocationFields);
    nlohmann::json time = nlohmann::json::object();
    JSONFIELDS::Write(time, binding.time, kTimeFields);
    node["location"] = std::move(location);
    node["time"] = std::move(time);
    return node;
}

} // namespace HIKARI::ENVIRONMENT
