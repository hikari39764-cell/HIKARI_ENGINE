#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

namespace HIKARI {

    class IComponent;

    enum class AuthoringFieldValueType : uint8_t {
        Bool,
        Int,
        Float,
        String,
        Color3,
        Vec2,
        Choice,
        AssetId,
        SceneId,
        SceneObjectId,
        InputActionId,
        RuntimeVariableId,
        RuntimeEventId,
    };

    enum class AuthoringEventPayloadPolicy : uint8_t {
        Any,
        None,
        Exact,
    };

    struct AuthoringFieldDescriptor {
        std::string_view id{};
        std::string_view label{};
        std::string_view description{};
        bool writable = true;
    };

    struct AuthoringChoice {
        std::string_view id{};
        std::string_view label{};
    };

    enum class AuthoringCommandParameterType : uint8_t {
        String,
        Float,
        Int,
        Bool,
        Json,
    };

    struct AuthoringCommandParameter {
        std::string name{};
        AuthoringCommandParameterType type =
            AuthoringCommandParameterType::String;
        bool required = false;
        std::string description{};
    };

    struct AuthoringCommandResult {
        bool ok = false;
        std::string code{};
        std::string message{};
        nlohmann::json data = nlohmann::json::object();
    };

    struct ComponentAuthoringCommandDefinition {
        std::string id{};
        std::string summary{};
        std::vector<AuthoringCommandParameter> parameters{};
        std::function<AuthoringCommandResult(
            IComponent&,
            const nlohmann::json&)> execute{};
    };

} // namespace HIKARI
