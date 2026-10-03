#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

#include "Assets/Sprite/HIKARI_SpriteMaterialAsset.h"

namespace HIKARI::SPRITE {

bool ParseSpriteMaterialParameterValues(const nlohmann::json& node,
                                        std::vector<ASSETS::SHADER::ShaderParameterOverride>& outValues,
                                        std::string& outError, std::string_view path = "parameters");
nlohmann::ordered_json WriteSpriteMaterialParameterValues(
    std::span<const ASSETS::SHADER::ShaderParameterOverride> values);

} // namespace HIKARI::SPRITE
