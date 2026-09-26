#pragma once

#include <DirectXMath.h>

#include "Vfx/Common/HIKARI_FxTypes.h"

namespace HIKARI::EDITOR {

// 効果のパラメーター定義に従い、ref が指す float4 スロットの成分を編集する。
bool DrawFxParameter(const VFX::ParamDesc& parameter, DirectX::XMFLOAT4& slotValue);

} // namespace HIKARI::EDITOR
