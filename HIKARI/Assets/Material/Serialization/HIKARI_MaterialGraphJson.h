#pragma once

#include <string>

#include <json.hpp>

#include "RenderContracts/Material/HIKARI_MaterialGraphData.h"

namespace HIKARI::MATERIAL_SERIALIZATION {

bool ReadMaterialValueJson(const nlohmann::json& node, MaterialValueData& outValue, std::string& outError);

nlohmann::json WriteMaterialValueJson(const MaterialValueData& value);

bool ReadMaterialParameterJson(const nlohmann::json& node, MaterialGraphParameterData& outParameter,
                               std::string& outError);

nlohmann::json WriteMaterialParameterJson(const MaterialGraphParameterData& parameter);

bool ReadMaterialGraphJson(const nlohmann::json& node, MaterialGraphData& outGraph, std::string& outError);

nlohmann::json WriteMaterialGraphJson(const MaterialGraphData& graph);

} // namespace HIKARI::MATERIAL_SERIALIZATION
