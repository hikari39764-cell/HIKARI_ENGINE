#pragma once

#include <json.hpp>

#include "Render3D/Core/HIKARI_CameraOptics.h"

namespace HIKARI::CAMERA {

void ReadDepthOfFieldSettings(const nlohmann::json& node, DepthOfFieldSettings& settings);
nlohmann::json WriteDepthOfFieldSettings(const DepthOfFieldSettings& settings);

} // namespace HIKARI::CAMERA
