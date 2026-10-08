#pragma once

#include <json.hpp>

#include "Render3D/Core/HIKARI_CameraOptics.h"

namespace HIKARI::CAMERA {

void ReadDepthOfFieldSettings(const nlohmann::json& node, DepthOfFieldSettings& settings);
nlohmann::json WriteDepthOfFieldSettings(const DepthOfFieldSettings& settings);
void ReadMotionBlurSettings(const nlohmann::json& node, MotionBlurSettings& settings);
nlohmann::json WriteMotionBlurSettings(const MotionBlurSettings& settings);

} // namespace HIKARI::CAMERA
