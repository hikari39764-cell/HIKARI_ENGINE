#pragma once

#include <string>
#include <vector>

#include <Windows.h>
#include <dxcapi.h>

#include "Gfx/HIKARI_ShaderCookCompiler.h"
#include "Gfx/Internal/HIKARI_DxcRuntime.h"

namespace HIKARI::GFX::INTERNAL {

    bool ValidateShaderCookDxil(
        const DxcRuntimeSnapshot& runtime,
        IDxcBlob* shader,
        std::string& outError);

    bool ExtractShaderCookBindings(
        const DxcRuntimeSnapshot& runtime,
        IDxcResult* result,
        ShaderStage stage,
        std::vector<ShaderCookBinding>& outBindings,
        std::string& outError);

} // namespace HIKARI::GFX::INTERNAL
