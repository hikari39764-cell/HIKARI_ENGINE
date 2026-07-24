#pragma once

#include <string>

#include <json.hpp>

#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"

namespace HIKARI::RENDER3D {

    nlohmann::json SerializeRenderQualitySettings(
        const RenderQualitySettings& settings);

    bool DeserializeRenderQualitySettings(
        const nlohmann::json& node,
        RenderQualitySettings& inOutSettings,
        std::string* errorMessage = nullptr);

} // namespace HIKARI::RENDER3D
