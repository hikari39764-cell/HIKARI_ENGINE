#include "Editor/SystemAuthoring/HIKARI_SystemAuthoringTypes.h"

namespace HIKARI::EDITOR {

    const char* ToDisplayName(SystemAuthoringScope scope) noexcept {
        switch (scope) {
        case SystemAuthoringScope::RuntimeOnly:
            return "Runtime implementation";
        case SystemAuthoringScope::ComponentOwned:
            return "Configured by components";
        case SystemAuthoringScope::SceneSettings:
            return "Scene settings";
        case SystemAuthoringScope::ProjectSettings:
            return "Project settings";
        case SystemAuthoringScope::DedicatedTool:
            return "Dedicated tool";
        }
        return "Unknown";
    }

} // namespace HIKARI::EDITOR
