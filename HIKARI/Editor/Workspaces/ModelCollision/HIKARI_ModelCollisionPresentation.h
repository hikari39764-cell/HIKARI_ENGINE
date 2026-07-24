#pragma once

#include <string_view>

#include "Assets/Collision/HIKARI_ModelCollisionSetup.h"

namespace HIKARI::EDITOR::MODEL_COLLISION_PRESENTATION {

inline constexpr float kMinimumPreviewSize = 64.0f;

const char *
ShapeTypeLabel(ASSETS::COLLISION::CollisionGeometryShapeType type) noexcept;

bool MatchesSearch(std::string_view text, std::string_view query);

} // namespace HIKARI::EDITOR::MODEL_COLLISION_PRESENTATION
