#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/Material/HIKARI_MaterialAssetData.h"

namespace HIKARI::MATERIAL_SERIALIZATION {
bool SerializeMaterialCompileInputJson(const MaterialAssetData& source, std::string& outJson, std::string& outError);

bool ParseMaterialSourceJson(std::string_view jsonText, MaterialAssetData& outData, std::string& outError,
                             std::vector<std::string>* outDiagnostics = nullptr);

bool SerializeMaterialSourceJson(const MaterialAssetData& data, std::string& outJsonText, std::string& outError);

bool LoadMaterialSourceJson(const std::filesystem::path& path, MaterialAssetData& outData, std::string& outError,
                            std::vector<std::string>* outDiagnostics = nullptr);

bool SaveMaterialSourceJson(const std::filesystem::path& path, const MaterialAssetData& data, std::string& outError);

} // namespace HIKARI::MATERIAL_SERIALIZATION
