#pragma once

#include <string>
#include <string_view>

#include "Assets/Material/Compiler/HIKARI_MaterialCompiler.h"

namespace HIKARI::MATERIAL_SERIALIZATION {

    bool SerializeMaterialCompileResult(
        const MaterialCompileResult& result,
        std::string& outJsonText,
        std::string& outError);

    bool ParseMaterialCompileResult(
        std::string_view jsonText,
        MaterialCompileResult& outResult,
        std::string& outError);

} // namespace HIKARI::MATERIAL_SERIALIZATION
