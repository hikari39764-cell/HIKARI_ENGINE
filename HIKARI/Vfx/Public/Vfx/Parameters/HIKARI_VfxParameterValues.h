#pragma once

#include <cstddef>
#include <vector>

#include <DirectXMath.h>

#include "Vfx/Common/HIKARI_FxTypes.h"

namespace HIKARI::VFX {

size_t GetParameterComponentCount(ParamType type) noexcept;
// パラメーターの値を ref の slot/channel 位置へ書く。範囲外のパラメーターは書かない。
void WriteParameterValue(const ParamDesc& parameter, const float* components, DirectX::XMFLOAT4* values,
                         size_t valueCount) noexcept;
void ResetParameterValuesFromDefaults(const std::vector<ParamDesc>& parameters, DirectX::XMFLOAT4* values,
                                      size_t valueCount);

} // namespace HIKARI::VFX
