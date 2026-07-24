#pragma once

#include <DirectXMath.h>

#include "Vfx/Common/HIKARI_FxTypes.h"

namespace HIKARI::EDITOR {

bool DrawPostProfileParameter(const VFX::ParamDesc &parameter,
                              DirectX::XMFLOAT4 &slotValue);

} // namespace HIKARI::EDITOR
