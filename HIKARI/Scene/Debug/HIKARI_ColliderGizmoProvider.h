#pragma once

#include <string_view>

namespace HIKARI {

    class ComponentGizmoRegistry;

    inline constexpr std::string_view kColliderGizmoProviderId =
        "Colliders";

    void RegisterColliderGizmoProvider(
        ComponentGizmoRegistry& registry);

} // namespace HIKARI
