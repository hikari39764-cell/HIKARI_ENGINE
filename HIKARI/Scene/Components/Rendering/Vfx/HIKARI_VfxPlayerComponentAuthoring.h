#pragma once

#include <vector>

#include "Authoring/HIKARI_ComponentAuthoringTypes.h"

namespace HIKARI {

struct InspectorContext;

AuthoringCommandResult ValidateVfxPlayerComponentAuthoring(const IComponent& component,
                                                           const InspectorContext& context);

std::vector<ComponentAuthoringCommandDefinition> BuildVfxPlayerComponentAuthoringCommands();

} // namespace HIKARI
