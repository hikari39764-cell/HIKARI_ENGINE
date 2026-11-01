#pragma once

#include <span>
#include <string>
#include <vector>

#include <json.hpp>

#include "RenderContracts/Shader/HIKARI_ShaderParameters.h"

namespace HIKARI::ASSETS::SHADER {

// Reads/writes the raw JSON value using the shared typed-value codec. The
// expected type is carried by the surrounding parameter definition.
bool ReadShaderParameterValue(const nlohmann::json& node, BINDING::ValueType type, BINDING::Value& outValue,
                              std::string& outError);
nlohmann::json WriteShaderParameterValue(const BINDING::Value& value);

bool ParseShaderParameterDefinitions(const nlohmann::json& node, std::vector<ShaderParameterDefinition>& outDefinitions,
                                     std::string& outError);
nlohmann::ordered_json WriteShaderParameterDefinitions(std::span<const ShaderParameterDefinition> definitions);

} // namespace HIKARI::ASSETS::SHADER
