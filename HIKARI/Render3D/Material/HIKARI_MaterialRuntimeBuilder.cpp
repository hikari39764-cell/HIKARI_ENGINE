#include "HIKARI_MaterialRuntimeBuilder.h"

#include <algorithm>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Core/HIKARI_Logger.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Render3D/Material/HIKARI_DefaultPbrResources.h"
#include "Render3D/Material/HIKARI_GpuMaterialTypes.h"
#include "Render3D/Material/HIKARI_MaterialTextureUsage.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgramRuntimeDataBuilder.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI {

namespace {
RuntimeTextureSlot DefaultSlotForUsage(MaterialTextureUsage usage) {
    switch (usage) {
    case MaterialTextureUsage::BaseColor:
    case MaterialTextureUsage::Occlusion:
    case MaterialTextureUsage::Specular:
    case MaterialTextureUsage::SpecularColor: return DefaultPbrResources::WhiteSlot();
    case MaterialTextureUsage::Normal: return DefaultPbrResources::FlatNormalSlot();
    case MaterialTextureUsage::MetallicRoughness: return DefaultPbrResources::MetallicRoughnessSlot();
    case MaterialTextureUsage::Emissive: return DefaultPbrResources::BlackSlot();
    default: return DefaultPbrResources::MissingSlot();
    }
}

RuntimeTextureSlot ActiveMissingSlot() {
    RuntimeTextureSlot slot = DefaultPbrResources::MissingSlot();
    slot.enabled = true;
    return slot;
}

RuntimeTextureSlot BuildSlot(const MaterialTextureSlotData& slotData, MaterialTextureUsage usage,
                             const AssetRegistry& assetRegistry, std::string_view debugName) {
    if (!slotData.useTexture || !slotData.textureAssetGuid.IsValid()) {
        return DefaultSlotForUsage(usage);
    }

    const auto* texture = assetRegistry.FindAs<TextureAssetDescriptor>(AssetId{slotData.textureAssetGuid.value});
    if (!texture || texture->sourcePath.empty()) {
        HIKARI_LOG_WARN("[MaterialRuntimeBuilder] texture asset not resolved: " + slotData.textureAssetGuid.value);
        return ActiveMissingSlot();
    }

    RuntimeTextureSlot slot{};
    slot.sourcePath = texture->sourcePath;
    slot.resolvedPath = texture->sourcePath;
    slot.texCoord = slotData.texCoord;
    slot.uvScale = slotData.uvScale;
    slot.uvOffset = slotData.uvOffset;
    slot.uvRotation = slotData.uvRotation;
    const std::string textureName = "material_asset/" + std::string(debugName) + "/" + MaterialTextureUsageName(usage);
    slot.resource =
        RENDER3D::LoadTextureResourceWithColorSpace(textureName, slot.resolvedPath, MaterialTextureColorSpace(usage));
    slot.handle = RENDER3D::GetTextureResourceBackendHandle(slot.resource);
    if (slot.handle < 0) {
        HIKARI_LOG_WARN("[MaterialRuntimeBuilder] texture load failed: " + slot.resolvedPath);
        return ActiveMissingSlot();
    }

    slot.enabled = true;
    return slot;
}

void ApplyPbrCompatibilityData(const PbrMaterialAssetData& data, const AssetRegistry& assetRegistry,
                               Material& outMaterial, std::string_view debugName) {
    outMaterial.SetBaseColor(data.baseColorFactor);
    outMaterial.SetMetallicFactor(data.metallicFactor);
    outMaterial.SetRoughnessFactor(data.roughnessFactor);
    outMaterial.SetSpecularFactor(data.specularFactor);
    outMaterial.SetSpecularColorFactor(data.specularColorFactor);
    outMaterial.SetNormalScale(data.normalScale);
    outMaterial.SetOcclusionStrength(data.occlusionStrength);
    outMaterial.SetEmissiveFactor(data.emissiveFactor);
    outMaterial.SetEmissiveStrength(data.emissiveStrength);

    outMaterial.SetTextureSlot(
        MaterialTextureUsage::BaseColor,
        BuildSlot(data.baseColorTexture, MaterialTextureUsage::BaseColor, assetRegistry, debugName));
    outMaterial.SetTextureSlot(MaterialTextureUsage::Normal,
                               BuildSlot(data.normalTexture, MaterialTextureUsage::Normal, assetRegistry, debugName));
    outMaterial.SetTextureSlot(
        MaterialTextureUsage::MetallicRoughness,
        BuildSlot(data.metallicRoughnessTexture, MaterialTextureUsage::MetallicRoughness, assetRegistry, debugName));
    outMaterial.SetTextureSlot(
        MaterialTextureUsage::Occlusion,
        BuildSlot(data.occlusionTexture, MaterialTextureUsage::Occlusion, assetRegistry, debugName));
    outMaterial.SetTextureSlot(
        MaterialTextureUsage::Emissive,
        BuildSlot(data.emissiveTexture, MaterialTextureUsage::Emissive, assetRegistry, debugName));
    outMaterial.SetTextureSlot(
        MaterialTextureUsage::Specular,
        BuildSlot(data.specularTexture, MaterialTextureUsage::Specular, assetRegistry, debugName));
    outMaterial.SetTextureSlot(
        MaterialTextureUsage::SpecularColor,
        BuildSlot(data.specularColorTexture, MaterialTextureUsage::SpecularColor, assetRegistry, debugName));
}

RuntimeMaterialAlphaMode ToRuntimeAlphaMode(MaterialAlphaMode alphaMode) noexcept {
    switch (alphaMode) {
    case MaterialAlphaMode::Mask: return RuntimeMaterialAlphaMode::Mask;
    case MaterialAlphaMode::Blend: return RuntimeMaterialAlphaMode::Blend;
    default: return RuntimeMaterialAlphaMode::Opaque;
    }
}

void ApplyProgramResolution(const PbrMaterialAssetData& pbr, const AssetRegistry& assetRegistry,
                            const RENDER3D::MATERIAL::MaterialProgramResolution& resolution, Material& outMaterial,
                            std::string_view debugName) {
    const MaterialProgramIR& program = resolution.effectiveProgram;
    const RENDER3D::MATERIAL::MaterialProgramRuntimeData runtimeData =
        RENDER3D::MATERIAL::BuildMaterialProgramRuntimeData(program, resolution.kind, assetRegistry, debugName);
    outMaterial.SetShaderProfileId("PBR");
    outMaterial.SetMaterialProgram(resolution.id);
    outMaterial.SetMaterialProgramParameterBlock(runtimeData.parameters);
    outMaterial.SetMaterialParameterProjection(runtimeData.parameterProjection);
    outMaterial.SetMaterialProgramResourceBlock(runtimeData.resources);
    outMaterial.SetMaterialProgramInstanceFlags(runtimeData.instanceFlags);
    outMaterial.SetMaterialAlphaMode(ToRuntimeAlphaMode(program.surface.alphaMode));
    outMaterial.SetAlphaCutoff(program.surface.alphaCutoff);
    outMaterial.SetDoubleSided(program.surface.doubleSided);
    outMaterial.SetUnlit(program.surface.unlit);
    outMaterial.SetShellSettings(program.shell);

    uint32_t featureBits = 0u;
    if (program.surface.unlit) {
        featureBits |= MATERIAL_FEATURES::Unlit;
    }
    if (program.surface.alphaMode == MaterialAlphaMode::Mask) {
        featureBits |= MATERIAL_FEATURES::AlphaMask;
    } else if (program.surface.alphaMode == MaterialAlphaMode::Blend) {
        featureBits |= MATERIAL_FEATURES::ThinTransparentSurface;
    }
    if (pbr.emissiveTexture.useTexture || pbr.emissiveStrength > 0.0f) {
        featureBits |= MATERIAL_FEATURES::Emissive;
    }
    outMaterial.SetFeatureBits(featureBits);
}
} // namespace

bool MaterialRuntimeBuilder::BuildRuntimeMaterial(const PbrMaterialAssetData& data, const AssetRegistry& assetRegistry,
                                                  Material& outMaterial, std::string_view debugName) const {
    outMaterial.SetBaseColor(data.baseColorFactor);
    outMaterial.SetMetallicFactor(data.metallicFactor);
    outMaterial.SetRoughnessFactor(data.roughnessFactor);
    outMaterial.SetSpecularFactor(data.specularFactor);
    outMaterial.SetSpecularColorFactor(data.specularColorFactor);
    outMaterial.SetNormalScale(data.normalScale);
    outMaterial.SetOcclusionStrength(data.occlusionStrength);
    outMaterial.SetEmissiveFactor(data.emissiveFactor);
    outMaterial.SetEmissiveStrength(data.emissiveStrength);
    outMaterial.SetShaderProfileId("PBR");

    uint32_t featureBits = 0;
    if (data.unlit) {
        featureBits |= MATERIAL_FEATURES::Unlit;
    }
    if (data.emissiveTexture.useTexture || data.emissiveStrength > 0.0f) {
        featureBits |= MATERIAL_FEATURES::Emissive;
    }
    outMaterial.SetFeatureBits(featureBits);

    // Material JSON は GUID だけを保持し、Runtime では Registry から cooked texture へ解決する。
    outMaterial.SetTextureSlot(
        MaterialTextureUsage::BaseColor,
        BuildSlot(data.baseColorTexture, MaterialTextureUsage::BaseColor, assetRegistry, debugName));
    outMaterial.SetTextureSlot(MaterialTextureUsage::Normal,
                               BuildSlot(data.normalTexture, MaterialTextureUsage::Normal, assetRegistry, debugName));
    outMaterial.SetTextureSlot(
        MaterialTextureUsage::MetallicRoughness,
        BuildSlot(data.metallicRoughnessTexture, MaterialTextureUsage::MetallicRoughness, assetRegistry, debugName));
    outMaterial.SetTextureSlot(
        MaterialTextureUsage::Occlusion,
        BuildSlot(data.occlusionTexture, MaterialTextureUsage::Occlusion, assetRegistry, debugName));
    outMaterial.SetTextureSlot(
        MaterialTextureUsage::Emissive,
        BuildSlot(data.emissiveTexture, MaterialTextureUsage::Emissive, assetRegistry, debugName));
    outMaterial.SetTextureSlot(
        MaterialTextureUsage::Specular,
        BuildSlot(data.specularTexture, MaterialTextureUsage::Specular, assetRegistry, debugName));
    outMaterial.SetTextureSlot(
        MaterialTextureUsage::SpecularColor,
        BuildSlot(data.specularColorTexture, MaterialTextureUsage::SpecularColor, assetRegistry, debugName));

    const MaterialAssetData source = MakeStandardPbrMaterialAssetData(data);
    const MaterialCompileResult compileResult = CompileMaterialAsset(source);
    const MaterialProgramIR effectiveProgram =
        compileResult.success ? compileResult.program : BuildStandardPbrMaterialProgramFallback();
    const RENDER3D::MATERIAL::MaterialProgramResolution resolution{
        RENDER3D::MATERIAL::kStandardPbrMaterialProgramId,
        compileResult.success ? RENDER3D::MATERIAL::MaterialProgramResolutionKind::Reused
                              : RENDER3D::MATERIAL::MaterialProgramResolutionKind::StandardPbrFallback,
        effectiveProgram, compileResult.diagnostics};
    ApplyProgramResolution(data, assetRegistry, resolution, outMaterial, debugName);

    return true;
}

bool MaterialRuntimeBuilder::BuildRuntimeMaterial(const MaterialAssetData& data, const AssetRegistry& assetRegistry,
                                                  RENDER3D::MATERIAL::MaterialProgramRegistry& programRegistry,
                                                  Material& outMaterial, std::string_view debugName,
                                                  RENDER3D::MATERIAL::MaterialProgramResolution* outResolution) const {
    return BuildRuntimeMaterial(data, CompileMaterialAsset(data), assetRegistry, programRegistry, outMaterial,
                                debugName, outResolution);
}

bool MaterialRuntimeBuilder::BuildRuntimeMaterial(const MaterialAssetData& data,
                                                  const MaterialCompileResult& compileResult,
                                                  const AssetRegistry& assetRegistry,
                                                  RENDER3D::MATERIAL::MaterialProgramRegistry& programRegistry,
                                                  Material& outMaterial, std::string_view debugName,
                                                  RENDER3D::MATERIAL::MaterialProgramResolution* outResolution) const {
    const RENDER3D::MATERIAL::MaterialProgramResolution resolution =
        programRegistry.ResolveCompileResult(debugName, compileResult);
    const PbrMaterialAssetData pbr = ProjectPbrMaterialAssetData(data);
    ApplyPbrCompatibilityData(pbr, assetRegistry, outMaterial, debugName);
    ApplyProgramResolution(pbr, assetRegistry, resolution, outMaterial, debugName);
    if (outResolution != nullptr) {
        *outResolution = resolution;
    }
    return resolution.id.IsValid();
}

} // namespace HIKARI
