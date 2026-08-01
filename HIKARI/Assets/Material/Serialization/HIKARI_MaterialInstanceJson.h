#pragma once

#include <string>

#include <json.hpp>

#include "Assets/Material/HIKARI_MaterialAssetData.h"

namespace HIKARI::MATERIAL_SERIALIZATION {

    bool ReadMaterialInstanceJson(
        const nlohmann::json& node,
        MaterialInstanceData& outInstance,
        std::string& outError);

    nlohmann::json WriteMaterialInstanceJson(
        const MaterialInstanceData& instance);

} // namespace HIKARI::MATERIAL_SERIALIZATION
