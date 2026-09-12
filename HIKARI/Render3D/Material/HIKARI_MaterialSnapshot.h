#pragma once

#include "Render3D/Material/HIKARI_GpuMaterialProjection.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI { class ModelAsset; }

namespace HIKARI::RENDER3D::MATERIAL {

    struct MaterialTextureSnapshotSource {
        TextureResourceLoadRequest load{};
        TextureResourceHandle resource{};
        bool enabled = false;
    };

    // 作者データをコピーする CPU snapshot。GPU lifetime は実行ジョブ側が保持する。
    struct MaterialSnapshot {
        GpuMaterialProjection projection{};
        std::array<MaterialTextureSnapshotSource, kGpuMaterialTextureUsages.size()> textures{};
        std::vector<MATH::Vec4> programParameters{};
        std::vector<MaterialTextureSnapshotSource> programTextures{};
        GpuMaterialProgramBinding programBinding{};
        bool hasProgram = false;
        bool supportsStaticEvaluation = true;
        std::string error{};
    };

    MaterialSnapshot CaptureMaterialSnapshot(
        const ModelAsset* model, const MaterialAsset* modelMaterial, const Material* runtimeMaterial);

} // namespace HIKARI::RENDER3D::MATERIAL
