#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/Material/Compiler/HIKARI_MaterialCompiler.h"

namespace HIKARI::ASSETS::MATERIAL {

using MaterialAssetLookup =
    std::function<bool(const AssetGuid& assetGuid, MaterialAssetData& outSource, std::string& outError)>;

struct MaterialInstanceResolveResult {
    bool success = false;
    MaterialAssetData resolvedMaterial{};
    MaterialCompileResult compile{};
    std::vector<MaterialCompileDiagnostic> diagnostics{};
};

MaterialInstanceResolveResult ResolveMaterialInstance(const MaterialAssetData& instanceSource,
                                                      const MaterialAssetLookup& lookup, uint32_t maxParentDepth = 16u,
                                                      const MaterialCompileOptions& compileOptions = {});

bool SetMaterialInstanceParameterOverride(MaterialAssetData& instanceSource, const MaterialAssetData& parentMaterial,
                                          std::string_view parameterId, const MaterialValueData& value,
                                          std::string& outError);

bool SetMaterialInstanceResourceOverride(MaterialAssetData& instanceSource, const MaterialAssetData& parentMaterial,
                                         std::string_view resourceId, const AssetGuid& assetGuid,
                                         std::string& outError);

bool RestoreMaterialInstanceParameterDefault(MaterialAssetData& instanceSource, std::string_view parameterId) noexcept;

bool RestoreMaterialInstanceResourceDefault(MaterialAssetData& instanceSource, std::string_view resourceId) noexcept;

} // namespace HIKARI::ASSETS::MATERIAL
