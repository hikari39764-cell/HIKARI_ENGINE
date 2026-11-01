#pragma once

#include <string_view>

#include "RenderContracts/Material/HIKARI_MaterialAssetData.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgramRegistry.h"

namespace HIKARI {

class AssetRegistry;
class Material;

class MaterialRuntimeBuilder {
public:
    // 固定の PBR 値だけで組む材質。標準 PBR のプログラムは registry の出どころ（Assets のコンパイラ）から作る。
    bool BuildRuntimeMaterial(const PbrMaterialAssetData& data, const AssetRegistry& assetRegistry,
                              RENDER3D::MATERIAL::MaterialProgramRegistry& programRegistry, Material& outMaterial,
                              std::string_view debugName) const;

    bool BuildRuntimeMaterial(const MaterialAssetData& data, const MaterialCompileResult& compileResult,
                              const AssetRegistry& assetRegistry,
                              RENDER3D::MATERIAL::MaterialProgramRegistry& programRegistry, Material& outMaterial,
                              std::string_view debugName,
                              RENDER3D::MATERIAL::MaterialProgramResolution* outResolution = nullptr) const;
};

} // namespace HIKARI
