#pragma once

#include <string_view>

namespace HIKARI {

class ComponentGizmoRegistry;

inline constexpr std::string_view kColliderGizmoProviderId = "Colliders";

// 形の枠：碰撞器と、流体の障碍物（FluidObstacleComponent の形と、使う碰撞器の形）。
void RegisterColliderGizmoProvider(ComponentGizmoRegistry& registry);
void RegisterFluidObstacleGizmoProvider(ComponentGizmoRegistry& registry);

} // namespace HIKARI
