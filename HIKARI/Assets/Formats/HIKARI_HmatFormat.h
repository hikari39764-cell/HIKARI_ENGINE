#pragma once

#include <filesystem>
#include <string>

#include "Assets/Material/Compiler/HIKARI_MaterialCompiler.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"

namespace HIKARI {

bool WriteHmatFile(const std::filesystem::path& path, const MaterialAssetData& material,
                   const MaterialCompileResult& compileResult, std::string& outMessage);

bool ReadHmatFile(const std::filesystem::path& path, MaterialAssetData& outMaterial,
                  MaterialCompileResult& outCompileResult, std::string& outMessage);

} // namespace HIKARI
