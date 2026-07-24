#pragma once

#include <cstddef>
#include <vector>

#include <DirectXMath.h>

#include "Vfx/Common/HIKARI_FxTypes.h"

namespace HIKARI::VFX {

    void ResetParameterValuesFromDefaults(
        const std::vector<ParamDesc>& parameters,
        DirectX::XMFLOAT4* values,
        size_t valueCount);

} // namespace HIKARI::VFX
