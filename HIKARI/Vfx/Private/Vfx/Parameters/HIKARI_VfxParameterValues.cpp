#include "Vfx/Parameters/HIKARI_VfxParameterValues.h"

#include <algorithm>

namespace HIKARI::VFX {

size_t GetParameterComponentCount(ParamType type) noexcept {
    switch (type) {
    case ParamType::Float2: return 2;
    case ParamType::Float3:
    case ParamType::Color3: return 3;
    case ParamType::Float4:
    case ParamType::Color:
    case ParamType::Color4: return 4;
    case ParamType::Float:
    case ParamType::Toggle:
    default: return 1;
    }
}

void WriteParameterValue(const ParamDesc& parameter, const float* components, DirectX::XMFLOAT4* values,
                         size_t valueCount) noexcept {
    const size_t slot = static_cast<size_t>(parameter.ref.slot);
    const size_t channel = static_cast<size_t>(parameter.ref.channel);
    if (values == nullptr || components == nullptr || slot >= valueCount || channel >= 4u) {
        return;
    }
    float* destination = &values[slot].x;
    const size_t writeCount = std::min<size_t>(4u - channel, GetParameterComponentCount(parameter.type));
    for (size_t index = 0; index < writeCount; ++index) {
        destination[channel + index] = components[index];
    }
}

void ResetParameterValuesFromDefaults(const std::vector<ParamDesc>& parameters, DirectX::XMFLOAT4* values,
                                      size_t valueCount) {
    if (values == nullptr) {
        return;
    }
    std::fill(values, values + valueCount, DirectX::XMFLOAT4{});
    for (const ParamDesc& parameter : parameters) {
        WriteParameterValue(parameter, parameter.defaultValues, values, valueCount);
    }
}

} // namespace HIKARI::VFX
