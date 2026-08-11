#pragma once

#include <span>
#include <string>

#include <json.hpp>

#include "Scene/HIKARI_SceneComponentId.h"
#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

    class ComponentRegistry;
    class IInspectorBuilder;
    struct InspectorContext;
    struct SceneDocument;
    struct SceneObjectData;

namespace EDITOR {

    struct ComponentAuthoringTarget {
        SceneObjectId objectId{};
        SceneComponentId componentId{};
    };

    struct ComponentPropertyMutation {
        ComponentAuthoringTarget target{};
        std::string propertyId{};
        nlohmann::json value{};
    };

    struct ComponentAuthoringServiceResult {
        bool ok = false;
        bool documentChanged = false;
        std::string code{};
        std::string message{};
        nlohmann::json data = nlohmann::json::object();
    };

    class ComponentPropertyAuthoringService {
    public:
        ComponentAuthoringServiceResult Describe(
            const ComponentRegistry& registry,
            const SceneDocument& document,
            ComponentAuthoringTarget target,
            InspectorContext context) const;

        ComponentAuthoringServiceResult Apply(
            const ComponentRegistry& registry,
            SceneDocument& document,
            std::span<const ComponentPropertyMutation> mutations,
            InspectorContext context) const;

        ComponentAuthoringServiceResult ExecuteCommand(
            const ComponentRegistry& registry,
            SceneDocument& document,
            ComponentAuthoringTarget target,
            std::string_view commandId,
            const nlohmann::json& arguments,
            InspectorContext context) const;
    };

} // namespace EDITOR
} // namespace HIKARI
