#pragma once

#include <cstdint>
#include <string_view>

#include "RenderContracts/Material/HIKARI_MaterialAssetData.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgramRegistry.h"

namespace HIKARI {

class AssetRegistry;
class Material;
class ModelAsset;

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

    // 取り込んだモデルの材質 materialIndex の写し。テクスチャはモデルから継承し、描き分け（シェーダー・特性・
    // 透明の扱い・両面）も元のまま。実行時に色だけ変える時（ModelView::SetMaterialColorOverride）の起点。
    bool BuildModelMaterialCopy(const ModelAsset& model, uint32_t materialIndex,
                                RENDER3D::MATERIAL::MaterialProgramRegistry& programRegistry,
                                Material& outMaterial) const;
};

} // namespace HIKARI
