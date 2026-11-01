#pragma once

#include <span>

#include "Render3D/Material/HIKARI_GpuMaterialProjection.h"
#include "RenderCore/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI {
class ModelAsset;
}

namespace HIKARI::RENDER3D::MATERIAL {

// 作者データをコピーする CPU snapshot。GPU lifetime は実行ジョブ側が保持する。
struct MaterialSnapshot {
    GpuMaterialProjection projection{};
    std::array<TextureSnapshotSource, kGpuMaterialTextureUsages.size()> textures{};
    std::vector<MATH::Vec4> programParameters{};
    std::vector<TextureSnapshotSource> programTextures{};
    GpuMaterialProgramBinding programBinding{};
    bool hasProgram = false;
    bool supportsStaticEvaluation = true;
    std::string error{};
};

MaterialSnapshot CaptureMaterialSnapshot(const ModelAsset* model, const MaterialAsset* modelMaterial,
                                         const Material* runtimeMaterial);

std::vector<TextureSnapshotSource> CollectMaterialSnapshotTextures(std::span<const MaterialSnapshot> materials);

} // namespace HIKARI::RENDER3D::MATERIAL
