#pragma once

#include <json.hpp>

namespace HIKARI::EDITOR {

// Apply a keyed TerrainSurface chunk delta to serialized component
// properties. The function is intentionally document/history agnostic so
// brush commits, workspace commands, tests, and future import tools share
// exactly one deterministic implementation.
bool ApplyTerrainSurfaceEditDelta(nlohmann::json& componentProperties, const nlohmann::json& delta);

} // namespace HIKARI::EDITOR
