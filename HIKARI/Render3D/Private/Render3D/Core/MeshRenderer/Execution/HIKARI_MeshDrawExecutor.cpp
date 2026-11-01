#include "Render3D/Core/MeshRenderer/Execution/HIKARI_MeshDrawExecutor.h"

#include <algorithm>
#include <vector>

#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Core/MeshRenderer/Data/HIKARI_MeshMaterialResolver.h"
#include "Render3D/Core/MeshRenderer/Data/HIKARI_MeshDrawDataBuilder.h"
#include "RenderContracts/Model/HIKARI_ModelAsset.h"
#include "Render3D/Material/HIKARI_GpuMaterialProjection.h"
#include "RenderCore/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/Runtime/HIKARI_SurfaceGpuScene.h"

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace HIKARI::MESHRENDERER {

namespace {
// GPU Scene は SurfaceRecord の通常経路として消費する、E
const MaterialAsset* GetPrimitiveMaterial(const ModelAsset& asset, uint32_t materialIndex) {
    if (materialIndex >= asset.materials.size()) {
        return nullptr;
    }
    return &asset.materials[materialIndex];
}

uint32_t ResolveTextureDescriptorIndex(int textureHandle) {
    const UINT descriptorIndex = RENDER3D::GetTextureResourceSrvDescriptorIndexFromBackendHandle(textureHandle);
    return descriptorIndex == UINT32_MAX ? kInvalidTextureDescriptorIndex : static_cast<uint32_t>(descriptorIndex);
}

void UpdateMaterialProgramBlocks(RENDER3D::MATERIAL::GpuMaterialRegistry& registry, uint32_t materialSlot,
                                 const Material& material) {
    const std::vector<uint32_t> resources =
        RENDER3D::MATERIAL::ResolveMaterialProgramResourceDescriptorIndices(material);
    if (registry.UpdateProgramBlocks(materialSlot, material.GetMaterialProgramId(),
                                     material.GetMaterialProgramParameterBlock(), resources,
                                     material.GetMaterialProgramInstanceFlags())) {
        return;
    }
    registry.UpdateProgramBlocks(materialSlot, RENDER3D::MATERIAL::kStandardPbrMaterialProgramId, {}, {},
                                 material.GetMaterialProgramInstanceFlags() |
                                     RENDER3D::MATERIAL::GPU_MATERIAL_PROGRAM_FLAGS::UsedFallback);
}

MaterialTextureDescriptorIndices ResolveMaterialTextureDescriptorIndices(const MaterialTextureHandles& textures) {
    MaterialTextureDescriptorIndices indices{};
    indices.baseColor = ResolveTextureDescriptorIndex(textures.baseColor);
    indices.normal = ResolveTextureDescriptorIndex(textures.normal);
    indices.emissive = ResolveTextureDescriptorIndex(textures.emissive);
    indices.metallicRoughness = ResolveTextureDescriptorIndex(textures.metallicRoughness);
    indices.occlusion = ResolveTextureDescriptorIndex(textures.occlusion);
    indices.specular = ResolveTextureDescriptorIndex(textures.specular);
    indices.specularColor = ResolveTextureDescriptorIndex(textures.specularColor);
    return indices;
}

bool HasSpecularTextureSlot(const MaterialAsset* materialAsset, const Material* runtimeMaterial,
                            MaterialTextureUsage usage) {
    if (runtimeMaterial != nullptr) {
        if (runtimeMaterial->HasTextureSlot(usage)) {
            return true;
        }
        if (!runtimeMaterial->InheritsModelTextures()) {
            return false;
        }
    }
    if (materialAsset == nullptr) {
        return false;
    }
    switch (usage) {
    case MaterialTextureUsage::Specular: return materialAsset->specularTexture.textureIndex >= 0;
    case MaterialTextureUsage::SpecularColor: return materialAsset->specularColorTexture.textureIndex >= 0;
    default: return false;
    }
}

MaterialGpuData BuildMaterialGpuData(const ObjectCB& obj, const MaterialTextureHandles& textures,
                                     const MaterialAsset* materialAsset, const Material* runtimeMaterial) {
    RENDER3D::MATERIAL::GpuMaterialProjection projection{};
    const MaterialTextureDescriptorIndices textureIndices = ResolveMaterialTextureDescriptorIndices(textures);
    projection.baseColor = obj.baseColor;
    projection.emissiveFactor = obj.emissiveFactor;
    projection.pbrParams = {obj.metallicFactor, obj.roughnessFactor, obj.occlusionStrength, obj.alphaCutoff};
    const RENDER3D::MATERIAL::MaterialFactorProjection factors =
        RENDER3D::MATERIAL::BuildMaterialFactorProjection(materialAsset, runtimeMaterial);
    projection.specularParams = factors.specularParams;
    projection.shellStroke0 = factors.shellStroke0;
    projection.shellStroke1 = factors.shellStroke1;
    projection.surfaceLayers = factors.surfaceLayers;
    projection.subsurface = factors.subsurface;
    projection.materialFlags = obj.materialFlags;
    projection.normalScale = obj.normalScale;

    const auto setTexture = [&](RENDER3D::MATERIAL::GpuMaterialTextureChannel channel, MaterialTextureUsage usage,
                                int handle, uint32_t descriptorIndex, bool enabled) {
        auto& texture = projection.textures[static_cast<size_t>(channel)];
        texture.backendHandle = handle;
        texture.descriptorIndex = descriptorIndex;
        RENDER3D::MATERIAL::ProjectMaterialTextureUv(materialAsset, runtimeMaterial, usage, texture);
        texture.enabled = enabled;
    };
    setTexture(RENDER3D::MATERIAL::GpuMaterialTextureChannel::BaseColor, MaterialTextureUsage::BaseColor,
               textures.baseColor, textureIndices.baseColor, obj.hasBaseColorTexture != 0u);
    setTexture(RENDER3D::MATERIAL::GpuMaterialTextureChannel::Normal, MaterialTextureUsage::Normal, textures.normal,
               textureIndices.normal, obj.hasNormalTexture != 0u);
    setTexture(RENDER3D::MATERIAL::GpuMaterialTextureChannel::Emissive, MaterialTextureUsage::Emissive,
               textures.emissive, textureIndices.emissive, obj.hasEmissiveTexture != 0u);
    setTexture(RENDER3D::MATERIAL::GpuMaterialTextureChannel::MetallicRoughness,
               MaterialTextureUsage::MetallicRoughness, textures.metallicRoughness, textureIndices.metallicRoughness,
               obj.hasMetallicRoughnessTexture != 0u);
    setTexture(RENDER3D::MATERIAL::GpuMaterialTextureChannel::Occlusion, MaterialTextureUsage::Occlusion,
               textures.occlusion, textureIndices.occlusion, obj.hasOcclusionTexture != 0u);
    setTexture(RENDER3D::MATERIAL::GpuMaterialTextureChannel::Specular, MaterialTextureUsage::Specular,
               textures.specular, textureIndices.specular,
               HasSpecularTextureSlot(materialAsset, runtimeMaterial, MaterialTextureUsage::Specular));
    setTexture(RENDER3D::MATERIAL::GpuMaterialTextureChannel::SpecularColor, MaterialTextureUsage::SpecularColor,
               textures.specularColor, textureIndices.specularColor,
               HasSpecularTextureSlot(materialAsset, runtimeMaterial, MaterialTextureUsage::SpecularColor));
    if (runtimeMaterial != nullptr) {
        // 筆触テクスチャは実行時材質だけが持つ。モデルの材質から継承する slot は無い。
        projection.textures[static_cast<size_t>(RENDER3D::MATERIAL::GpuMaterialTextureChannel::StrokeMask)] =
            RENDER3D::MATERIAL::ProjectRuntimeMaterialTexture(*runtimeMaterial, MaterialTextureUsage::StrokeMask);
        projection.textures[static_cast<size_t>(RENDER3D::MATERIAL::GpuMaterialTextureChannel::StrokeNormal)] =
            RENDER3D::MATERIAL::ProjectRuntimeMaterialTexture(*runtimeMaterial, MaterialTextureUsage::StrokeNormal);
    }
    return RENDER3D::MATERIAL::PackGpuMaterialData(projection);
}

MaterialTextureHandles ResolveRuntimeMaterialTextureHandles(const Material* material,
                                                            MaterialTextureHandles textureHandles, bool& outComplete) {
    if (material == nullptr) {
        return textureHandles;
    }

    const auto resolveSlot = [&outComplete](const RuntimeTextureSlot& slot, int inheritedHandle) {
        if (!slot.enabled) {
            return inheritedHandle;
        }
        const int resolved = slot.ResolveBackendHandle();
        if (resolved < 0) {
            outComplete = false;
            return inheritedHandle;
        }
        return resolved;
    };

    textureHandles.baseColor =
        resolveSlot(material->GetTextureSlot(MaterialTextureUsage::BaseColor), textureHandles.baseColor);
    textureHandles.normal = resolveSlot(material->GetTextureSlot(MaterialTextureUsage::Normal), textureHandles.normal);
    textureHandles.emissive =
        resolveSlot(material->GetTextureSlot(MaterialTextureUsage::Emissive), textureHandles.emissive);
    textureHandles.metallicRoughness = resolveSlot(material->GetTextureSlot(MaterialTextureUsage::MetallicRoughness),
                                                   textureHandles.metallicRoughness);
    textureHandles.occlusion =
        resolveSlot(material->GetTextureSlot(MaterialTextureUsage::Occlusion), textureHandles.occlusion);
    textureHandles.specular =
        resolveSlot(material->GetTextureSlot(MaterialTextureUsage::Specular), textureHandles.specular);
    textureHandles.specularColor =
        resolveSlot(material->GetTextureSlot(MaterialTextureUsage::SpecularColor), textureHandles.specularColor);
    return textureHandles;
}

void FillRuntimeMaterialValues(ObjectCB& obj, const Material& material) {
    FillMaterialFactorValues(obj, nullptr, &material);
    obj.hasBaseColorTexture = material.HasBaseColorTexture() ? 1u : 0u;
    obj.hasNormalTexture = material.HasTextureSlot(MaterialTextureUsage::Normal) ? 1u : 0u;
    obj.hasMetallicRoughnessTexture = material.HasTextureSlot(MaterialTextureUsage::MetallicRoughness) ? 1u : 0u;
    obj.hasOcclusionTexture = material.HasTextureSlot(MaterialTextureUsage::Occlusion) ? 1u : 0u;
    obj.hasEmissiveTexture = material.HasTextureSlot(MaterialTextureUsage::Emissive) ? 1u : 0u;
}

ResolvedMaterialTextures ResolveGpuSceneMaterialSourceTextures(
    const MeshDrawContext& ctx, const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source,
    const MaterialAsset* materialAsset) {
    ResolvedMaterialTextures textures{};
    if (source.materialOverride != nullptr) {
        if (source.materialOverride->InheritsModelTextures() && ctx.services.materialResolver != nullptr &&
            source.model != nullptr) {
            textures = ctx.services.materialResolver->Resolve(*source.model, materialAsset, ctx.services.stats);
        } else {
            textures.baseColor = ctx.binding.fallbackTextureHandle;
            textures.normal = ctx.binding.fallbackNormalTextureHandle;
            textures.emissive = ctx.materialFill.fallbackBlackTextureHandle;
            textures.metallicRoughness = ctx.binding.fallbackTextureHandle;
            textures.occlusion = ctx.binding.fallbackTextureHandle;
            textures.specular = ctx.binding.fallbackTextureHandle;
            textures.specularColor = ctx.binding.fallbackTextureHandle;
            textures.complete = true;
        }
        bool complete = textures.complete;
        const MaterialTextureHandles inherited{textures.baseColor,         textures.normal,    textures.emissive,
                                               textures.metallicRoughness, textures.occlusion, textures.specular,
                                               textures.specularColor};
        const MaterialTextureHandles handles =
            ResolveRuntimeMaterialTextureHandles(source.materialOverride, inherited, complete);
        textures.baseColor = handles.baseColor;
        textures.normal = handles.normal;
        textures.emissive = handles.emissive;
        textures.metallicRoughness = handles.metallicRoughness;
        textures.occlusion = handles.occlusion;
        textures.specular = handles.specular;
        textures.specularColor = handles.specularColor;
        textures.complete = complete;
        return textures;
    }

    if (ctx.services.materialResolver != nullptr && source.model != nullptr) {
        return ctx.services.materialResolver->Resolve(*source.model, materialAsset, ctx.services.stats);
    }

    textures.baseColor = ctx.binding.fallbackTextureHandle;
    textures.normal = ctx.binding.fallbackNormalTextureHandle;
    textures.emissive = ctx.materialFill.fallbackBlackTextureHandle;
    textures.metallicRoughness = ctx.binding.fallbackTextureHandle;
    textures.occlusion = ctx.binding.fallbackTextureHandle;
    textures.specular = ctx.binding.fallbackTextureHandle;
    textures.specularColor = ctx.binding.fallbackTextureHandle;
    return textures;
}

MaterialTextureHandles ToMaterialTextureHandles(const ResolvedMaterialTextures& textures) {
    return {textures.baseColor, textures.normal,   textures.emissive,     textures.metallicRoughness,
            textures.occlusion, textures.specular, textures.specularColor};
}

void BindSurfaceRecordFrameResourcesInternal(const MeshDrawContext& ctx) {
    BindFrameCommonResources(ctx.binding, ctx.staticRootSig, ctx.cameraAddress, ctx.cullingCameraAddress,
                             ctx.lightAddress, ctx.localLightsAddress, ctx.localLightClusterHeadersAddress,
                             ctx.localLightClusterIndicesAddress, ctx.materialProgramParametersAddress,
                             ctx.materialProgramResourcesAddress, ctx.shadowAddress, ctx.skyEnvironmentAddress);
    BindObjectDataBuffer(ctx.binding, ctx.objectDataSrv);
    BindMaterialDataBuffer(ctx.binding, ctx.materialDataSrv);
    BindSurfaceGpuSceneBuffer(ctx.binding, ctx.surfaceGpuSceneSrv);
    BindSurfaceGpuSceneControl(ctx.binding, 0u, false);
    BindSurfaceFeatureData(ctx.binding, ctx.surfaceFeatureDataAddress);
    BindTerrainSurfaceData(ctx.binding, ctx.terrainSurfaceDataAddress);
    BindSurfaceFeatureFloats(ctx.binding, ctx.surfaceFeatureFloatsAddress);
    BindOceanSpectrumData(ctx.binding, ctx.oceanSpectrumTable, ctx.oceanSpectrumValid, ctx.oceanSpectrumResolution);
    BindShadowMap(ctx.binding);
    if (ctx.passKind == MeshDrawPassKind::Forward) {
        BindSkyCube(ctx.binding);
        BindSceneDepth(ctx.binding);
        BindSceneColor(ctx.binding);
        BindScreenSpaceTraceResources(ctx.binding);
        BindIblResources(ctx.binding);
        BindReflectionProbeResources(ctx.binding);
        BindSsao(ctx.binding);
        BindLightProbeResources(ctx.binding);
    } else if (ctx.passKind == MeshDrawPassKind::WaterVisibility) {
        // The independent water depth pass samples authoritative
        // SceneDepth only to reject occluded water pixels.
        BindSceneDepth(ctx.binding);
    }
}

bool BuildGpuSceneMaterialSourceData(const MeshDrawContext& ctx,
                                     const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source,
                                     MaterialGpuData& outData, bool& outFinalized) {
    if (source.model == nullptr) {
        return false;
    }

    const MaterialAsset* materialAsset = GetPrimitiveMaterial(*source.model, source.materialIndex);
    const ResolvedMaterialTextures textures = ResolveGpuSceneMaterialSourceTextures(ctx, source, materialAsset);

    ObjectCB obj{};
    obj.world = source.world;
    obj.normalMatrix = source.normalMatrix;
    FillMaterialValues(obj, materialAsset, textures.normal, textures.emissive, textures.metallicRoughness,
                       textures.occlusion, ctx.materialFill);
    if (source.materialOverride != nullptr) {
        FillRuntimeMaterialValues(obj, *source.materialOverride);
    }
    obj.hasBaseColorTexture =
        (textures.baseColor >= 0 && textures.baseColor != ctx.binding.fallbackTextureHandle) ? 1u : 0u;
    obj.hasNormalTexture =
        (textures.normal >= 0 && textures.normal != ctx.binding.fallbackNormalTextureHandle) ? 1u : 0u;
    obj.hasEmissiveTexture =
        (textures.emissive >= 0 && textures.emissive != ctx.materialFill.fallbackBlackTextureHandle) ? 1u : 0u;
    obj.hasMetallicRoughnessTexture =
        (textures.metallicRoughness >= 0 && textures.metallicRoughness != ctx.binding.fallbackTextureHandle) ? 1u : 0u;
    obj.hasOcclusionTexture =
        (textures.occlusion >= 0 && textures.occlusion != ctx.binding.fallbackTextureHandle) ? 1u : 0u;
    obj.receiveShadow = source.receiveShadow ? 1u : 0u;
    obj.fxFlags = source.fxFlags;
    for (size_t i = 0; i < VFX::kMaterialFxUserCount; ++i) {
        obj.fxUser[i] = source.fxUser[i];
    }

    const MaterialTextureHandles textureHandles = ToMaterialTextureHandles(textures);
    outData = BuildMaterialGpuData(obj, textureHandles, materialAsset, source.materialOverride);
    outFinalized = textures.complete;
    return true;
}

} // namespace

void BindSurfaceRecordFrameResources(const MeshDrawContext& ctx) {
    // SurfaceRecord は frame 共通リソースめEplan 単位で束縛する、E
    BindSurfaceRecordFrameResourcesInternal(ctx);
}

bool PrepareSurfaceGpuSceneMaterialSources(const MeshDrawContext& ctx,
                                           const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource* sources,
                                           size_t sourceCount) {
    if (ctx.gpuMaterialRegistry == nullptr || sources == nullptr) {
        return false;
    }

    bool resolvedAny = false;
    for (size_t sourceIndex = 0; sourceIndex < sourceCount; ++sourceIndex) {
        const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source = sources[sourceIndex];
        const bool hasSourceRecord = source.sourceRecordIndex != RENDER3D::RUNTIME::kInvalidRenderSurfaceIndex;
        if (!hasSourceRecord) {
            continue;
        }
        const RENDER3D::MATERIAL::GpuMaterialSourceKey sourceKey{source.materialResource,
                                                                 source.materialKey,
                                                                 reinterpret_cast<uintptr_t>(source.model),
                                                                 reinterpret_cast<uintptr_t>(source.materialOverride),
                                                                 source.materialRevision,
                                                                 source.materialIndex};

        uint32_t materialSlot = kInvalidMaterialDataIndex;
        if (ctx.gpuMaterialRegistry->TryReuseSourceBinding(source.sourceRecordIndex, sourceKey, materialSlot)) {
            resolvedAny = true;
            continue;
        }

        MaterialGpuData materialData{};
        bool finalized = true;
        if (!BuildGpuSceneMaterialSourceData(ctx, source, materialData, finalized)) {
            continue;
        }

        materialSlot =
            ctx.gpuMaterialRegistry->ResolveAndBindSource(source.sourceRecordIndex, sourceKey, materialData, finalized);
        if (materialSlot != kInvalidMaterialDataIndex && source.materialOverride != nullptr) {
            UpdateMaterialProgramBlocks(*ctx.gpuMaterialRegistry, materialSlot, *source.materialOverride);
        }
        resolvedAny = materialSlot != kInvalidMaterialDataIndex || resolvedAny;
    }

    return resolvedAny;
}

} // namespace HIKARI::MESHRENDERER
