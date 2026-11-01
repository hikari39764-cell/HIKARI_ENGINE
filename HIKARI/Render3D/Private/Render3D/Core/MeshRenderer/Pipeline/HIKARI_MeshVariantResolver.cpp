#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshVariantResolver.h"

#include <utility>

#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Material/HIKARI_GpuMaterialProjection.h"
#include "RenderContracts/Model/HIKARI_ModelAsset.h"

namespace HIKARI::MESHRENDERER {

namespace {
} // namespace

void ApplyProfileToVariant(const MaterialFxProfile& profile, VFX::VariantKey& variant) {
    if (!profile.shaderProfileId.empty()) {
        variant.shaderId = profile.shaderProfileId;
    }
    if (!profile.vertexShaderId.empty()) {
        variant.vertexShaderId = profile.vertexShaderId;
    }
    if (!profile.pixelShaderId.empty()) {
        variant.pixelShaderId = profile.pixelShaderId;
    }
    variant.featureBits = profile.featureBits;
    variant.composite = profile.composite;
    variant.depthTest = profile.depthTest;
    variant.depthWrite = profile.depthWrite;
    variant.doubleSided = profile.doubleSided;
}

VFX::VariantKey ResolvePrimitiveVariant(const DrawItem& item, const MaterialAsset* materialAsset,
                                        const MeshPrimitive* primitiveAsset) {
    VFX::VariantKey variant = item.variant;
    const bool materialAlphaBlend =
        RENDER3D::MATERIAL::ResolveMaterialAlphaMode(materialAsset, item.materialOverride) == AlphaMode::Blend;

    if (item.materialOverride != nullptr) {
        variant.shaderId = item.materialOverride->GetShaderProfileId();
        variant.featureBits = item.materialOverride->GetFeatureBits();
        variant.doubleSided = item.materialOverride->IsDoubleSided();
    } else if (materialAsset != nullptr) {
        variant.shaderId = materialAsset->shaderProfileId;
        variant.featureBits = materialAsset->featureBits;
        variant.doubleSided = primitiveAsset != nullptr
                                  ? SURFACE_POLICY::ShouldRenderDoubleSided(*materialAsset, *primitiveAsset)
                                  : MATERIAL_POLICY::ShouldRenderDoubleSided(*materialAsset);
    }

    if (item.hasResolvedMaterialFxProfile) {
        ApplyProfileToVariant(item.resolvedMaterialFxProfile, variant);
        if (materialAsset != nullptr) {
            const bool materialDoubleSided =
                primitiveAsset != nullptr ? SURFACE_POLICY::ShouldRenderDoubleSided(*materialAsset, *primitiveAsset)
                                          : MATERIAL_POLICY::ShouldRenderDoubleSided(*materialAsset);
            if (materialDoubleSided) {
                variant.doubleSided = true;
            }
            if ((materialAsset->featureBits & MATERIAL_FEATURES::AlphaMask) != 0) {
                variant.featureBits |= MATERIAL_FEATURES::AlphaMask;
            }
        }
    }

    if (materialAlphaBlend) {
        variant.composite = VFX::CompositeMode::Alpha;
        variant.depthWrite = false;
    }

    return variant;
}

} // namespace HIKARI::MESHRENDERER
