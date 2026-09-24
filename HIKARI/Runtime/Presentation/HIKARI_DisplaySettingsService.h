#pragma once
#include "HIKARI/Project/DisplaySettingsTypes.h"
namespace HIKARI {
class World;
}
namespace HIKARI::SERVICES {
bool ReadDisplaySettings(GameProjectDisplaySettings& out) noexcept;
bool ReadDisplayCapabilities(GameProjectDisplayCapabilities& out) noexcept;
GameProjectDisplayApplyResult ApplyDisplaySettings(World& world, const GameProjectDisplaySettings& settings) noexcept;
} // namespace HIKARI::SERVICES
