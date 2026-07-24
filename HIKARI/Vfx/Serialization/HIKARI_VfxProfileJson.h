#pragma once

#include <json.hpp>

#include "Vfx/Common/HIKARI_FxTypes.h"

namespace HIKARI::VFX::SERIALIZATION {

    FxDomain ParseFxDomain(
        const nlohmann::json& valueNode,
        FxDomain fallback);

    CompositeMode ParseCompositeMode(
        const nlohmann::json& valueNode,
        CompositeMode fallback);

} // namespace HIKARI::VFX::SERIALIZATION
