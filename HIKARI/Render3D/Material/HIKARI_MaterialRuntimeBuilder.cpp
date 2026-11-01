#include "HIKARI_MaterialRuntimeBuilder.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "RenderContracts/Asset/HIKARI_AssetRegistry.h"
#include "RenderContracts/Asset/HIKARI_AssetTypes.h"
#include "Core/HIKARI_Logger.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "RenderContracts/Model/HIKARI_ModelAsset.h"
#include "Render3D/Material/HIKARI_DefaultPbrResources.h"
#include "Render3D/Material/HIKARI_GpuMaterialTypes.h"
#include "Render3D/Material/HIKARI_MaterialTextureUsage.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgramRuntimeDataBuilder.h"
#include "RenderCore/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI {

namespace {
RuntimeTextureSlot DefaultSlotForUsage(MaterialTextureUsage usage) {
    switch (usage) {
    case MaterialTextureUsage::BaseColor:
    case MaterialTextureUsage::Occlusion:
    case MaterialTextureUsage::Specular:
    case MaterialTextureUsage::SpecularColor: return DefaultPbrResources::WhiteSlot();
    case MaterialTextureUsage::Normal:
    case MaterialTextureUsage::StrokeNormal: return DefaultPbrResources::FlatNormalSlot();
    case MaterialTextureUsage::StrokeMask: return DefaultPbrResources::WhiteSlot();
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

    const auto* texture = assetRegistry.FindAs<TextureAssetDescriptor>(slotData.textureAssetGuid);
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
    outMaterial.SetSurfaceLayers(
        {data.clearCoatFactor, data.clearCoatRoughnessFactor, data.transmissionFactor, data.ior});
    outMaterial.SetSubsurface({data.subsurfaceFactor, data.subsurfaceRadius});

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

// 筆触の殻の 2 枚のテクスチャは Shell ノードが所有する resource から実行時材質の slot へ投影する。
// 殻を持たない材質は既定（白のマスク・平らな法線）へ戻し、同じ Material を別の資産で作り直しても残らない。
void ApplyPainterlyShellResources(const MaterialProgramIR& program, const AssetRegistry& assetRegistry,
                                  Material& outMaterial, std::string_view debugName) {
    constexpr std::pair<MaterialResourceSemantic, MaterialTextureUsage> kStrokeTextures[]{
        {MaterialResourceSemantic::StrokeMask, MaterialTextureUsage::StrokeMask},
        {MaterialResourceSemantic::StrokeNormal, MaterialTextureUsage::StrokeNormal},
    };
    for (const auto& [semantic, usage] : kStrokeTextures) {
        MaterialTextureSlotData slotData{};
        const auto resource = std::find_if(program.resources.begin(), program.resources.end(),
                                           [semantic](const auto& entry) { return entry.semantic == semantic; });
        if (resource != program.resources.end()) {
            slotData.useTexture = resource->assetGuid.IsValid();
            slotData.textureAssetGuid = resource->assetGuid;
            slotData.texCoord = static_cast<int>(resource->texCoord);
            slotData.uvScale = resource->uvScale;
            slotData.uvOffset = resource->uvOffset;
            slotData.uvRotation = resource->uvRotation;
        }
        outMaterial.SetTextureSlot(usage, BuildSlot(slotData, usage, assetRegistry, debugName));
    }
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
    ApplyPainterlyShellResources(program, assetRegistry, outMaterial, debugName);

    uint32_t featureBits = 0u;
    if (program.surface.unlit) {
        featureBits |= MATERIAL_FEATURES::Unlit;
    }
    if ((program.capabilities & MaterialCapabilityBit(MaterialProgramCapability::Shell)) != 0u) {
        featureBits |= MATERIAL_FEATURES::PainterlyShell;
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

RENDER3D::MATERIAL::MaterialProgramResolution StandardPbrResolution(
    const MaterialCompileResult& compileResult, const RENDER3D::MATERIAL::MaterialProgramRegistry& programRegistry) {
    return {RENDER3D::MATERIAL::kStandardPbrMaterialProgramId,
            compileResult.success ? RENDER3D::MATERIAL::MaterialProgramResolutionKind::Reused
                                  : RENDER3D::MATERIAL::MaterialProgramResolutionKind::StandardPbrFallback,
            compileResult.success ? compileResult.program : programRegistry.GetStandardPbrFallback().ir,
            compileResult.diagnostics};
}

// テクスチャを標準 PBR の入力へ直結するだけのグラフ（MakeStandardPbrMaterialAssetData と同じ構造）は、
// ProjectPbrMaterialAssetData の slot と係数で固定経路がそのまま描ける。専用 program にすると画素ごとに式を
// 解釈するので（網目の床で 1 フレーム数十 ms）、構造が一致するものは program 0 へ寄せる。
bool IsStandardPbrForm(const MaterialAssetData& data, const PbrMaterialAssetData& pbr,
                       const MaterialCompileResult& compileResult,
                       const RENDER3D::MATERIAL::MaterialProgramRegistry& programRegistry) {
    if (!compileResult.success || compileResult.usedFallback || data.kind != MaterialAssetKind::Material ||
        data.templateId != MaterialTemplateId::StandardPbr) {
        return false;
    }
    MaterialAssetData standard = MakeStandardPbrMaterialAssetData(pbr);
    standard.surface = data.surface;
    const MaterialCompileResult standardCompile = programRegistry.CompileStandardForm(standard);
    return standardCompile.success && standardCompile.program.key == compileResult.program.key;
}
} // namespace

bool MaterialRuntimeBuilder::BuildRuntimeMaterial(const PbrMaterialAssetData& data, const AssetRegistry& assetRegistry,
                                                  RENDER3D::MATERIAL::MaterialProgramRegistry& programRegistry,
                                                  Material& outMaterial, std::string_view debugName) const {
    ApplyPbrCompatibilityData(data, assetRegistry, outMaterial, debugName);
    ApplyProgramResolution(
        data, assetRegistry,
        StandardPbrResolution(programRegistry.CompileStandardForm(MakeStandardPbrMaterialAssetData(data)),
                              programRegistry),
        outMaterial, debugName);
    return true;
}

bool MaterialRuntimeBuilder::BuildRuntimeMaterial(const MaterialAssetData& data,
                                                  const MaterialCompileResult& compileResult,
                                                  const AssetRegistry& assetRegistry,
                                                  RENDER3D::MATERIAL::MaterialProgramRegistry& programRegistry,
                                                  Material& outMaterial, std::string_view debugName,
                                                  RENDER3D::MATERIAL::MaterialProgramResolution* outResolution) const {
    const PbrMaterialAssetData pbr = ProjectPbrMaterialAssetData(data);
    const RENDER3D::MATERIAL::MaterialProgramResolution resolution =
        IsStandardPbrForm(data, pbr, compileResult, programRegistry)
            ? StandardPbrResolution(compileResult, programRegistry)
            : programRegistry.ResolveCompileResult(debugName, compileResult);
    ApplyPbrCompatibilityData(pbr, assetRegistry, outMaterial, debugName);
    ApplyProgramResolution(pbr, assetRegistry, resolution, outMaterial, debugName);
    if (outResolution != nullptr) {
        *outResolution = resolution;
    }
    return resolution.id.IsValid();
}

} // namespace HIKARI
