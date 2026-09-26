#pragma once

#include <cstdint>
#include <string>

#include <json.hpp>

#include "Assets/Material/HIKARI_MaterialAssetData.h"

namespace HIKARI::MATERIAL_SERIALIZATION {

PbrMaterialAssetData ReadPbrMaterialJson(const nlohmann::json& node, std::string materialName);

nlohmann::json WritePbrMaterialJson(const PbrMaterialAssetData& data);

} // namespace HIKARI::MATERIAL_SERIALIZATION
