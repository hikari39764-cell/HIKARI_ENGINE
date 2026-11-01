#pragma once

#include "HIKARI/Project/ValueTypes.h"
#include "Core/Math/HIKARI_Transform3D.h"

namespace HIKARI::PROJECT::INTERNAL {

bool IsValidProjectTransform(const GameProjectTransform& transform) noexcept;
bool TryToRuntimeTransform(const GameProjectTransform& transform, Transform3D& outTransform) noexcept;
Transform3D ToRuntimeTransform(const GameProjectTransform& transform) noexcept;
GameProjectTransform ToProjectTransform(const Transform3D& transform) noexcept;

} // namespace HIKARI::PROJECT::INTERNAL
