#pragma once

#include <utility>

#include "Editor/Authoring/HIKARI_ComponentPropertyAuthoringService.h"

namespace HIKARI {
class IComponent;
}

namespace HIKARI::EDITOR {

inline ComponentAuthoringServiceResult MakeFailure(std::string code, std::string message) {
    ComponentAuthoringServiceResult result{};
    result.code = std::move(code);
    result.message = std::move(message);
    return result;
}

ComponentAuthoringServiceResult DescribeComponentProperties(IComponent& component, const InspectorContext& context);
ComponentAuthoringServiceResult ApplyComponentProperty(IComponent& component, const InspectorContext& context,
                                                       const ComponentPropertyMutation& mutation);

} // namespace HIKARI::EDITOR
