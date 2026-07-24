#pragma once

#include <vector>

#include <json.hpp>

#include "Vfx/Common/HIKARI_FxTypes.h"

namespace HIKARI::VFX::SERIALIZATION {

    void ReadParameterDescriptorsFromJson(
        const nlohmann::json& parameterArray,
        std::vector<ParamDesc>& outParameters);

} // namespace HIKARI::VFX::SERIALIZATION
