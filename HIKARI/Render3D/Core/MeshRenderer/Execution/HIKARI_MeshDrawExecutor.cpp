#include "Render3D/Core/MeshRenderer/Execution/HIKARI_MeshDrawExecutor.h"

#include <algorithm>

#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Core/MeshRenderer/Data/HIKARI_MeshMaterialResolver.h"
#include "Render3D/Core/MeshRenderer/Data/HIKARI_MeshDrawDataBuilder.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
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
            const UINT descriptorIndex =
                RENDER3D::GetTextureResourceSrvDescriptorIndexFromBackendHandle(textureHandle);
            return descriptorIndex == UINT32_MAX
                ? kInvalidTextureDescriptorIndex
                : static_cast<uint32_t>(descriptorIndex);
        }

        MaterialTextureDescriptorIndices ResolveMaterialTextureDescriptorIndices(
            const MaterialTextureHandles& textures) {

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

        MATH::Vec4 ResolveSpecularParams(
            const MaterialAsset* materialAsset,
            const Material* runtimeMaterial) {

            MATH::Vec3 specularColor{ 1.0f, 1.0f, 1.0f };
            float specularFactor = 1.0f;
            if (materialAsset != nullptr) {
                specularColor = materialAsset->specularColorFactor;
                specularFactor = materialAsset->specularFactor;
            }
            if (runtimeMaterial != nullptr) {
                specularColor = runtimeMaterial->GetSpecularColorFactor();
                specularFactor = runtimeMaterial->GetSpecularFactor();
            }
            return {
                specularColor.x,
                specularColor.y,
                specularColor.z,
                specularFactor
            };
        }

        MATH::Vec4 DefaultUvTransform() {
            return { 1.0f, 1.0f, 0.0f, 0.0f };
        }

        MATH::Vec4 ToUvTransform(const TextureSlot& slot) {
            return {
                slot.uvScale.x,
                slot.uvScale.y,
                slot.uvOffset.x,
                slot.uvOffset.y
            };
        }

        MATH::Vec4 ToUvTransform(const RuntimeTextureSlot& slot) {
            return {
                slot.uvScale.x,
                slot.uvScale.y,
                slot.uvOffset.x,
                slot.uvOffset.y
            };
        }

        uint32_t ToUvSet(int texCoord) {
            return static_cast<uint32_t>(std::clamp(texCoord, 0, 1));
        }

        MATH::Vec4 ResolveSlotUvTransform(
            const MaterialAsset* materialAsset,
            const Material* runtimeMaterial,
            MaterialTextureUsage usage) {

            if (runtimeMaterial != nullptr) {
                return ToUvTransform(runtimeMaterial->GetTextureSlot(usage));
            }
            if (materialAsset == nullptr) {
                return DefaultUvTransform();
            }
            switch (usage) {
            case MaterialTextureUsage::BaseColor: return ToUvTransform(materialAsset->baseColorTexture);
            case MaterialTextureUsage::Normal: return ToUvTransform(materialAsset->normalTexture);
            case MaterialTextureUsage::MetallicRoughness: return ToUvTransform(materialAsset->metallicRoughnessTexture);
            case MaterialTextureUsage::Occlusion: return ToUvTransform(materialAsset->occlusionTexture);
            case MaterialTextureUsage::Emissive: return ToUvTransform(materialAsset->emissiveTexture);
            case MaterialTextureUsage::Specular: return ToUvTransform(materialAsset->specularTexture);
            case MaterialTextureUsage::SpecularColor: return ToUvTransform(materialAsset->specularColorTexture);
            default: return DefaultUvTransform();
            }
        }

        float ResolveSlotUvRotation(
            const MaterialAsset* materialAsset,
            const Material* runtimeMaterial,
            MaterialTextureUsage usage) {

            if (runtimeMaterial != nullptr) {
                return runtimeMaterial->GetTextureSlot(usage).uvRotation;
            }
            if (materialAsset == nullptr) {
                return 0.0f;
            }
            switch (usage) {
            case MaterialTextureUsage::BaseColor: return materialAsset->baseColorTexture.uvRotation;
            case MaterialTextureUsage::Normal: return materialAsset->normalTexture.uvRotation;
            case MaterialTextureUsage::MetallicRoughness: return materialAsset->metallicRoughnessTexture.uvRotation;
            case MaterialTextureUsage::Occlusion: return materialAsset->occlusionTexture.uvRotation;
            case MaterialTextureUsage::Emissive: return materialAsset->emissiveTexture.uvRotation;
            case MaterialTextureUsage::Specular: return materialAsset->specularTexture.uvRotation;
            case MaterialTextureUsage::SpecularColor: return materialAsset->specularColorTexture.uvRotation;
            default: return 0.0f;
            }
        }

        uint32_t ResolveSlotUvSet(
            const MaterialAsset* materialAsset,
            const Material* runtimeMaterial,
            MaterialTextureUsage usage) {

            if (runtimeMaterial != nullptr) {
                return ToUvSet(runtimeMaterial->GetTextureSlot(usage).texCoord);
            }
            if (materialAsset == nullptr) {
                return 0u;
            }
            switch (usage) {
            case MaterialTextureUsage::BaseColor: return ToUvSet(materialAsset->baseColorTexture.texCoord);
            case MaterialTextureUsage::Normal: return ToUvSet(materialAsset->normalTexture.texCoord);
            case MaterialTextureUsage::MetallicRoughness: return ToUvSet(materialAsset->metallicRoughnessTexture.texCoord);
            case MaterialTextureUsage::Occlusion: return ToUvSet(materialAsset->occlusionTexture.texCoord);
            case MaterialTextureUsage::Emissive: return ToUvSet(materialAsset->emissiveTexture.texCoord);
            case MaterialTextureUsage::Specular: return ToUvSet(materialAsset->specularTexture.texCoord);
            case MaterialTextureUsage::SpecularColor: return ToUvSet(materialAsset->specularColorTexture.texCoord);
            default: return 0u;
            }
        }

        bool HasSpecularTextureSlot(
            const MaterialAsset* materialAsset,
            const Material* runtimeMaterial,
            MaterialTextureUsage usage) {

            if (runtimeMaterial != nullptr) {
                return runtimeMaterial->HasTextureSlot(usage);
            }
            if (materialAsset == nullptr) {
                return false;
            }
            switch (usage) {
            case MaterialTextureUsage::Specular:
                return materialAsset->specularTexture.textureIndex >= 0;
            case MaterialTextureUsage::SpecularColor:
                return materialAsset->specularColorTexture.textureIndex >= 0;
            default:
                return false;
            }
        }

        MaterialGpuData BuildMaterialGpuData(
            const ObjectCB& obj,
            const MaterialTextureHandles& textures,
            const MaterialAsset* materialAsset,
            const Material* runtimeMaterial) {

            MaterialGpuData data{};
            const MaterialTextureDescriptorIndices textureIndices =
                ResolveMaterialTextureDescriptorIndices(textures);
            data.baseColor = obj.baseColor;
            data.emissiveFactor = obj.emissiveFactor;
            data.pbrParams = {
                obj.metallicFactor,
                obj.roughnessFactor,
                obj.occlusionStrength,
                obj.alphaCutoff
            };
            data.specularParams = ResolveSpecularParams(materialAsset, runtimeMaterial);
            data.materialFlags = obj.materialFlags;
            data.hasBaseColorTexture = obj.hasBaseColorTexture;
            data.hasNormalTexture = obj.hasNormalTexture;
            data.hasEmissiveTexture = obj.hasEmissiveTexture;
            data.hasMetallicRoughnessTexture = obj.hasMetallicRoughnessTexture;
            data.hasOcclusionTexture = obj.hasOcclusionTexture;
            data.hasSpecularTexture =
                HasSpecularTextureSlot(materialAsset, runtimeMaterial, MaterialTextureUsage::Specular) ? 1u : 0u;
            data.hasSpecularColorTexture =
                HasSpecularTextureSlot(materialAsset, runtimeMaterial, MaterialTextureUsage::SpecularColor) ? 1u : 0u;
            data.normalScale = obj.normalScale;
            data.baseColorTextureHandle = textures.baseColor;
            data.normalTextureHandle = textures.normal;
            data.emissiveTextureHandle = textures.emissive;
            data.metallicRoughnessTextureHandle = textures.metallicRoughness;
            data.occlusionTextureHandle = textures.occlusion;
            data.specularTextureHandle = textures.specular;
            data.specularColorTextureHandle = textures.specularColor;
            data.baseColorTextureDescriptorIndex = textureIndices.baseColor;
            data.normalTextureDescriptorIndex = textureIndices.normal;
            data.emissiveTextureDescriptorIndex = textureIndices.emissive;
            data.metallicRoughnessTextureDescriptorIndex = textureIndices.metallicRoughness;
            data.occlusionTextureDescriptorIndex = textureIndices.occlusion;
            data.specularTextureDescriptorIndex = textureIndices.specular;
            data.specularColorTextureDescriptorIndex = textureIndices.specularColor;
            data.baseColorUvTransform = ResolveSlotUvTransform(materialAsset, runtimeMaterial, MaterialTextureUsage::BaseColor);
            data.normalUvTransform = ResolveSlotUvTransform(materialAsset, runtimeMaterial, MaterialTextureUsage::Normal);
            data.emissiveUvTransform = ResolveSlotUvTransform(materialAsset, runtimeMaterial, MaterialTextureUsage::Emissive);
            data.metallicRoughnessUvTransform = ResolveSlotUvTransform(materialAsset, runtimeMaterial, MaterialTextureUsage::MetallicRoughness);
            data.occlusionUvTransform = ResolveSlotUvTransform(materialAsset, runtimeMaterial, MaterialTextureUsage::Occlusion);
            data.specularUvTransform = ResolveSlotUvTransform(materialAsset, runtimeMaterial, MaterialTextureUsage::Specular);
            data.specularColorUvTransform = ResolveSlotUvTransform(materialAsset, runtimeMaterial, MaterialTextureUsage::SpecularColor);
            data.uvRotation0 = {
                ResolveSlotUvRotation(materialAsset, runtimeMaterial, MaterialTextureUsage::BaseColor),
                ResolveSlotUvRotation(materialAsset, runtimeMaterial, MaterialTextureUsage::Normal),
                ResolveSlotUvRotation(materialAsset, runtimeMaterial, MaterialTextureUsage::Emissive),
                ResolveSlotUvRotation(materialAsset, runtimeMaterial, MaterialTextureUsage::MetallicRoughness)
            };
            data.uvRotation1 = {
                ResolveSlotUvRotation(materialAsset, runtimeMaterial, MaterialTextureUsage::Occlusion),
                ResolveSlotUvRotation(materialAsset, runtimeMaterial, MaterialTextureUsage::Specular),
                ResolveSlotUvRotation(materialAsset, runtimeMaterial, MaterialTextureUsage::SpecularColor),
                0.0f
            };
            data.uvSet0[0] = ResolveSlotUvSet(materialAsset, runtimeMaterial, MaterialTextureUsage::BaseColor);
            data.uvSet0[1] = ResolveSlotUvSet(materialAsset, runtimeMaterial, MaterialTextureUsage::Normal);
            data.uvSet0[2] = ResolveSlotUvSet(materialAsset, runtimeMaterial, MaterialTextureUsage::Emissive);
            data.uvSet0[3] = ResolveSlotUvSet(materialAsset, runtimeMaterial, MaterialTextureUsage::MetallicRoughness);
            data.uvSet1[0] = ResolveSlotUvSet(materialAsset, runtimeMaterial, MaterialTextureUsage::Occlusion);
            data.uvSet1[1] = ResolveSlotUvSet(materialAsset, runtimeMaterial, MaterialTextureUsage::Specular);
            data.uvSet1[2] = ResolveSlotUvSet(materialAsset, runtimeMaterial, MaterialTextureUsage::SpecularColor);
            return data;
        }

        void RecordMaterialTexturePoolStats(
            const MeshDrawContext& ctx,
            const MaterialGpuData& data) {

            MeshRendererDebugStats* stats = ctx.services.stats;
            if (stats == nullptr) {
                return;
            }

            const uint32_t descriptorIndices[] = {
                data.baseColorTextureDescriptorIndex,
                data.normalTextureDescriptorIndex,
                data.emissiveTextureDescriptorIndex,
                data.metallicRoughnessTextureDescriptorIndex,
                data.occlusionTextureDescriptorIndex,
                data.specularTextureDescriptorIndex,
                data.specularColorTextureDescriptorIndex,
            };

            for (uint32_t descriptorIndex : descriptorIndices) {
                ++stats->materialTexturePoolSlotCount;
                if (descriptorIndex == kInvalidTextureDescriptorIndex) {
                    ++stats->materialTexturePoolInvalidSlotCount;
                } else {
                    ++stats->materialTexturePoolResolvedSlotCount;
                }
            }
        }

        MaterialTextureHandles ResolveRuntimeMaterialTextureHandles(
            const Material* material,
            const MeshBindingContext& binding,
            const MeshMaterialFillContext& fill) {

            MaterialTextureHandles textureHandles{};
            textureHandles.baseColor = binding.fallbackTextureHandle;
            textureHandles.normal = binding.fallbackNormalTextureHandle;
            textureHandles.emissive = fill.fallbackBlackTextureHandle;
            textureHandles.metallicRoughness = binding.fallbackTextureHandle;
            textureHandles.occlusion = binding.fallbackTextureHandle;
            textureHandles.specular = binding.fallbackTextureHandle;
            textureHandles.specularColor = binding.fallbackTextureHandle;

            if (material == nullptr) {
                return textureHandles;
            }
            if (material->HasBaseColorTexture()) {
                textureHandles.baseColor = material->GetBaseColorTextureHandle();
            }
            if (material->HasTextureSlot(MaterialTextureUsage::Normal)) {
                textureHandles.normal = material->GetTextureSlot(MaterialTextureUsage::Normal).handle;
            }
            if (material->HasTextureSlot(MaterialTextureUsage::Emissive)) {
                textureHandles.emissive = material->GetTextureSlot(MaterialTextureUsage::Emissive).handle;
            }
            if (material->HasTextureSlot(MaterialTextureUsage::MetallicRoughness)) {
                textureHandles.metallicRoughness = material->GetTextureSlot(MaterialTextureUsage::MetallicRoughness).handle;
            }
            if (material->HasTextureSlot(MaterialTextureUsage::Occlusion)) {
                textureHandles.occlusion = material->GetTextureSlot(MaterialTextureUsage::Occlusion).handle;
            }
            if (material->HasTextureSlot(MaterialTextureUsage::Specular)) {
                textureHandles.specular = material->GetTextureSlot(MaterialTextureUsage::Specular).handle;
            }
            if (material->HasTextureSlot(MaterialTextureUsage::SpecularColor)) {
                textureHandles.specularColor = material->GetTextureSlot(MaterialTextureUsage::SpecularColor).handle;
            }
            return textureHandles;
        }

        void FillRuntimeMaterialValues(ObjectCB& obj, const Material& material) {
            obj.baseColor = material.GetBaseColor();
            obj.hasBaseColorTexture = material.HasBaseColorTexture() ? 1u : 0u;
            obj.hasNormalTexture = material.HasTextureSlot(MaterialTextureUsage::Normal) ? 1u : 0u;
            obj.hasMetallicRoughnessTexture = material.HasTextureSlot(MaterialTextureUsage::MetallicRoughness) ? 1u : 0u;
            obj.hasOcclusionTexture = material.HasTextureSlot(MaterialTextureUsage::Occlusion) ? 1u : 0u;
            obj.hasEmissiveTexture = material.HasTextureSlot(MaterialTextureUsage::Emissive) ? 1u : 0u;
            obj.normalScale = material.GetNormalScale();
            obj.metallicFactor = material.GetMetallicFactor();
            obj.roughnessFactor = material.GetRoughnessFactor();
            obj.occlusionStrength = material.GetOcclusionStrength();
            const MATH::Vec3& emissive = material.GetEmissiveFactor();
            obj.emissiveFactor = {
                emissive.x,
                emissive.y,
                emissive.z,
                material.GetEmissiveStrength()
            };
            obj.materialFlags = material.GetFeatureBits();
        }


        ResolvedMaterialTextures ResolveGpuSceneMaterialSourceTextures(
            const MeshDrawContext& ctx,
            const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source,
            const MaterialAsset* materialAsset) {

            ResolvedMaterialTextures textures{};
            if (source.materialOverride != nullptr) {
                const MaterialTextureHandles handles =
                    ResolveRuntimeMaterialTextureHandles(source.materialOverride, ctx.binding, ctx.materialFill);
                textures.baseColor = handles.baseColor;
                textures.normal = handles.normal;
                textures.emissive = handles.emissive;
                textures.metallicRoughness = handles.metallicRoughness;
                textures.occlusion = handles.occlusion;
                textures.specular = handles.specular;
                textures.specularColor = handles.specularColor;
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
            return {
                textures.baseColor,
                textures.normal,
                textures.emissive,
                textures.metallicRoughness,
                textures.occlusion,
                textures.specular,
                textures.specularColor
            };
        }

        void BindSurfaceRecordFrameResourcesInternal(const MeshDrawContext& ctx) {

            BindFrameCommonResources(
                ctx.binding,
                ctx.staticRootSig,
                ctx.cameraAddress,
                ctx.cullingCameraAddress,
                ctx.lightAddress,
                ctx.shadowAddress,
                ctx.skyEnvironmentAddress);
            BindObjectDataBuffer(ctx.binding, ctx.objectDataSrv);
            BindMaterialDataBuffer(ctx.binding, ctx.materialDataSrv);
            BindSurfaceGpuSceneBuffer(ctx.binding, ctx.surfaceGpuSceneSrv);
            BindSurfaceGpuSceneControl(ctx.binding, 0u, false);
            BindShadowMap(ctx.binding);
            if (ctx.passKind == MeshDrawPassKind::Forward) {
                BindSkyCube(ctx.binding);
                BindSceneDepth(ctx.binding);
                BindSceneColor(ctx.binding);
                BindIblResources(ctx.binding);
                BindReflectionProbeResources(ctx.binding);
                BindSsao(ctx.binding);
                BindLightProbeResources(ctx.binding);
            }
        }

        bool BuildGpuSceneMaterialSourceData(
            const MeshDrawContext& ctx,
            const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source,
            MaterialGpuData& outData,
            bool& outFinalized) {

            if (source.model == nullptr) {
                return false;
            }

            const MaterialAsset* materialAsset =
                GetPrimitiveMaterial(*source.model, source.materialIndex);
            const ResolvedMaterialTextures textures =
                ResolveGpuSceneMaterialSourceTextures(ctx, source, materialAsset);

            ObjectCB obj{};
            obj.world = source.world;
            obj.normalMatrix = source.normalMatrix;
            FillMaterialValues(
                obj,
                materialAsset,
                textures.normal,
                textures.emissive,
                textures.metallicRoughness,
                textures.occlusion,
                ctx.materialFill);
            if (source.materialOverride != nullptr) {
                FillRuntimeMaterialValues(obj, *source.materialOverride);
            }
            obj.hasBaseColorTexture =
                (textures.baseColor >= 0 && textures.baseColor != ctx.binding.fallbackTextureHandle) ? 1u : 0u;
            obj.receiveShadow = source.receiveShadow ? 1u : 0u;
            obj.fxFlags = source.fxFlags;
            for (size_t i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                obj.fxUser[i] = source.fxUser[i];
            }

            const MaterialTextureHandles textureHandles = ToMaterialTextureHandles(textures);
            outData = BuildMaterialGpuData(
                obj,
                textureHandles,
                materialAsset,
                source.materialOverride);
            RecordMaterialTexturePoolStats(ctx, outData);
            outFinalized = textures.complete;
            return true;
        }

    } // namespace

    void BindSurfaceRecordFrameResources(const MeshDrawContext& ctx) {
        // SurfaceRecord は frame 共通リソースめEplan 単位で束縛する、E
        BindSurfaceRecordFrameResourcesInternal(ctx);
    }

    bool PrepareSurfaceGpuSceneMaterialSources(
        const MeshDrawContext& ctx,
        const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource* sources,
        size_t sourceCount) {

        if (ctx.gpuMaterialRegistry == nullptr || sources == nullptr) {
            return false;
        }

        bool resolvedAny = false;
        for (size_t sourceIndex = 0; sourceIndex < sourceCount; ++sourceIndex) {
            const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source =
                sources[sourceIndex];
            const bool hasSourceRecord =
                source.sourceRecordIndex !=
                RENDER3D::RUNTIME::kInvalidRenderSurfaceIndex;
            if (!hasSourceRecord) {
                continue;
            }
            const RENDER3D::MATERIAL::GpuMaterialSourceKey sourceKey{
                source.materialResource,
                source.materialKey,
                reinterpret_cast<uintptr_t>(source.model),
                reinterpret_cast<uintptr_t>(source.materialOverride),
                source.materialRevision,
                source.materialIndex
            };

            uint32_t materialSlot = kInvalidMaterialDataIndex;
            if (ctx.gpuMaterialRegistry->TryReuseSourceBinding(
                    source.sourceRecordIndex,
                    sourceKey,
                    materialSlot)) {
                resolvedAny = true;
                continue;
            }

            MaterialGpuData materialData{};
            bool finalized = true;
            if (!BuildGpuSceneMaterialSourceData(
                    ctx,
                    source,
                    materialData,
                    finalized)) {
                continue;
            }

            materialSlot =
                ctx.gpuMaterialRegistry->ResolveAndBindSource(
                    source.sourceRecordIndex,
                    sourceKey,
                    materialData,
                    finalized);
            resolvedAny =
                materialSlot != kInvalidMaterialDataIndex || resolvedAny;
        }

        return resolvedAny;
    }

} // namespace HIKARI::MESHRENDERER
