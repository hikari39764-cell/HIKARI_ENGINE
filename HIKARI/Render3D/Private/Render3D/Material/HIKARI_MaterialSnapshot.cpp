#include "Render3D/Material/HIKARI_MaterialSnapshot.h"

#include "RenderContracts/Model/HIKARI_ModelAsset.h"
#include "Render3D/Material/Program/HIKARI_GpuMaterialProgramBinding.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgramRegistry.h"

namespace HIKARI::RENDER3D::MATERIAL {
namespace {
TextureSnapshotSource CaptureRuntimeTexture(const RuntimeTextureSlot& slot, TextureResourceColorSpace colorSpace) {
    TextureSnapshotSource result{};
    result.enabled = slot.enabled;
    if (!result.enabled)
        return result;
    result.load.path = GetMaterialTextureRuntimePath(slot);
    result.load.name = "material_snapshot/" + result.load.path;
    result.load.colorSpace = colorSpace;
    result.resource = slot.resource;
    const int backendHandle = slot.ResolveBackendHandle();
    if (!result.resource && backendHandle >= 0)
        result.resource = RegisterTextureResourceFromBackendHandle(backendHandle);
    return result;
}
} // namespace

std::vector<TextureSnapshotSource> CollectMaterialSnapshotTextures(std::span<const MaterialSnapshot> materials) {
    std::vector<TextureSnapshotSource> sources;
    for (const auto& material : materials) {
        for (const auto& source : material.textures)
            if (source.enabled)
                sources.push_back(source);
        for (const auto& source : material.programTextures)
            if (source.enabled)
                sources.push_back(source);
    }
    return sources;
}

MaterialSnapshot CaptureMaterialSnapshot(const ModelAsset* model, const MaterialAsset* modelMaterial,
                                         const Material* runtimeMaterial) {
    MaterialSnapshot result;
    result.projection = GpuMaterialProjection{BuildMaterialFactorProjection(modelMaterial, runtimeMaterial)};
    for (size_t i = 0; i < kGpuMaterialTextureUsages.size(); ++i) {
        const auto usage = kGpuMaterialTextureUsages[i];
        auto& texture = result.textures[i];
        ProjectMaterialTextureUv(modelMaterial, runtimeMaterial, usage, result.projection.textures[i]);
        if (const auto* slot = ResolveMaterialTextureOverride(runtimeMaterial, usage)) {
            texture = CaptureRuntimeTexture(*slot, MaterialTextureColorSpace(usage));
        } else if (const auto* modelSlot = GetModelMaterialTextureSlot(modelMaterial, usage)) {
            texture.enabled = modelSlot->textureIndex >= 0;
            texture.load.colorSpace = MaterialTextureColorSpace(usage);
            if (texture.enabled && model != nullptr &&
                static_cast<size_t>(modelSlot->textureIndex) < model->textures.size()) {
                texture.load.path =
                    GetMaterialTextureRuntimePath(model->textures[static_cast<size_t>(modelSlot->textureIndex)]);
                texture.load.name = "material_snapshot/" + texture.load.path;
            }
        }
    }
    if (runtimeMaterial != nullptr && runtimeMaterial->GetMaterialProgramId()) {
        result.hasProgram = true;
        if (!BuildGpuMaterialProgramBinding(*runtimeMaterial, {}, result.programBinding))
            result.error = "Invalid material program binding.";
        const auto* program = GetRuntimeMaterialProgramRegistry().Find(runtimeMaterial->GetMaterialProgramId());
        if (program == nullptr)
            result.error = "Material program is no longer registered.";
        else
            result.supportsStaticEvaluation = program->capabilities.SupportsStaticSurfaceEvaluation();
        const auto parameters = runtimeMaterial->GetMaterialProgramParameterBlock();
        result.programParameters.assign(parameters.begin(), parameters.end());
        for (const auto& slot : runtimeMaterial->GetMaterialProgramResourceBlock())
            result.programTextures.push_back(CaptureRuntimeTexture(slot, TextureResourceColorSpace::Auto));
    }
    return result;
}

} // namespace HIKARI::RENDER3D::MATERIAL
