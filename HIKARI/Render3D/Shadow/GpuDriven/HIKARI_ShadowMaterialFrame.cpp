#include "Render3D/Shadow/Internal/HIKARI_ShadowRendererInternal.h"

#include <string>
#include <vector>

#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::SHADOW::INTERNAL {

    const MaterialAsset* GetPrimitiveMaterial(
        const ModelAsset& asset,
        uint32_t materialIndex) {

        if (materialIndex >= asset.materials.size()) {
            return nullptr;
        }
        return &asset.materials[static_cast<size_t>(materialIndex)];
    }

    RENDER3D::TextureResourceHandle ResolvePrimitiveTextureResource(const ModelAsset& asset, const MaterialAsset* materialAsset) {
        if (materialAsset == nullptr ||
            materialAsset->baseColorTexture.textureIndex < 0 ||
            materialAsset->baseColorTexture.textureIndex >= static_cast<int>(asset.textures.size())) {
            return gShadowRendererState.fallbackTextureResource;
        }

        const TextureAsset3D& texture = asset.textures[static_cast<size_t>(materialAsset->baseColorTexture.textureIndex)];
        if (texture.sourcePath.empty()) {
            return gShadowRendererState.fallbackTextureResource;
        }

        const std::string cacheKey = "shadow:base:" + texture.sourcePath;
        auto found = gShadowRendererState.materialTextureCache.find(cacheKey);
        if (found != gShadowRendererState.materialTextureCache.end() &&
            RENDER3D::IsTextureResourceValid(found->second)) {
            return found->second;
        }

        RENDER3D::TextureResourceHandle resource =
            RENDER3D::LoadTextureResourceSrgb(cacheKey, texture.sourcePath);
        gShadowRendererState.materialTextureCache[cacheKey] = resource;
        return RENDER3D::IsTextureResourceValid(resource) ? resource : gShadowRendererState.fallbackTextureResource;
    }

    uint32_t ResolveTextureDescriptorIndex(int textureHandle) {
        const UINT descriptorIndex =
            RENDER3D::GetTextureResourceSrvDescriptorIndexFromBackendHandle(textureHandle);
        return descriptorIndex == UINT32_MAX
            ? MESHRENDERER::kInvalidTextureDescriptorIndex
            : static_cast<uint32_t>(descriptorIndex);
    }
    uint32_t UploadShadowMaterialData(
        uint64_t key,
        const MESHRENDERER::MaterialGpuData& data) {

        MESHRENDERER::MaterialGpuData* materialData =
            GetActiveShadowFrameResources().materialDataMapped;
        if (materialData == nullptr) {
            return MESHRENDERER::kInvalidMaterialDataIndex;
        }

        auto found = gShadowRendererState.materialDataFrameTable.indexByKey.find(key);
        if (found != gShadowRendererState.materialDataFrameTable.indexByKey.end()) {
            return found->second;
        }

        if (gShadowRendererState.materialDataFrameTable.count >= MESHRENDERER::kMaxMaterialDataCount) {
            return MESHRENDERER::kInvalidMaterialDataIndex;
        }

        const uint32_t index = gShadowRendererState.materialDataFrameTable.count++;
        materialData[index] = data;
        gShadowRendererState.materialDataFrameTable.indexByKey.emplace(key, index);
        if (data.baseColorTextureDescriptorIndex != MESHRENDERER::kInvalidTextureDescriptorIndex) {
            gShadowRendererState.materialDataFrameTable.textureDescriptorIndices.insert(data.baseColorTextureDescriptorIndex);
        }
        return index;
    }

    uint64_t BuildShadowMaterialDataKey(
        uint64_t stableMaterialKey,
        const MESHRENDERER::MaterialGpuData& data) {

        uint64_t seed = AppendShadowHashValue(1469598103934665603ull, stableMaterialKey);
        return AppendShadowHashBytes(seed, &data, sizeof(data));
    }

    void ResetShadowMaterialFrame() {
        gShadowRendererState.materialDataFrameTable.Clear();
        if (GetActiveShadowFrameResources().materialDataMapped == nullptr) {
            return;
        }

        MESHRENDERER::MaterialGpuData defaultData{};
        defaultData.baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        defaultData.pbrParams = { 0.0f, 1.0f, 1.0f, 0.5f };
        defaultData.normalScale = 1.0f;
        defaultData.baseColorTextureHandle = gShadowRendererState.fallbackTextureHandle;
        defaultData.normalTextureHandle = -1;
        defaultData.emissiveTextureHandle = -1;
        defaultData.metallicRoughnessTextureHandle = -1;
        defaultData.occlusionTextureHandle = -1;
        defaultData.baseColorTextureDescriptorIndex =
            ResolveTextureDescriptorIndex(gShadowRendererState.fallbackTextureHandle);
        defaultData.normalTextureDescriptorIndex = MESHRENDERER::kInvalidTextureDescriptorIndex;
        defaultData.emissiveTextureDescriptorIndex = MESHRENDERER::kInvalidTextureDescriptorIndex;
        defaultData.metallicRoughnessTextureDescriptorIndex = MESHRENDERER::kInvalidTextureDescriptorIndex;
        defaultData.occlusionTextureDescriptorIndex = MESHRENDERER::kInvalidTextureDescriptorIndex;
        (void)UploadShadowMaterialData(0u, defaultData);
    }

    MESHRENDERER::MaterialGpuData BuildShadowMaterialGpuData(
        const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source) {

        const MaterialAsset* materialAsset =
            source.model != nullptr
                ? GetPrimitiveMaterial(*source.model, source.materialIndex)
                : nullptr;
        const RENDER3D::TextureResourceHandle baseColorTexture =
            source.model != nullptr
                ? ResolvePrimitiveTextureResource(*source.model, materialAsset)
                : gShadowRendererState.fallbackTextureResource;

        MESHRENDERER::MaterialGpuData data{};
        data.baseColor = materialAsset != nullptr
            ? materialAsset->baseColorFactor
            : MATH::Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
        data.emissiveFactor = {
            materialAsset != nullptr ? materialAsset->emissiveFactor.x : 0.0f,
            materialAsset != nullptr ? materialAsset->emissiveFactor.y : 0.0f,
            materialAsset != nullptr ? materialAsset->emissiveFactor.z : 0.0f,
            materialAsset != nullptr ? materialAsset->emissiveStrength : 1.0f
        };
        data.pbrParams = {
            materialAsset != nullptr ? materialAsset->metallicFactor : 0.0f,
            materialAsset != nullptr ? materialAsset->roughnessFactor : 1.0f,
            materialAsset != nullptr ? materialAsset->occlusionTexture.strength : 1.0f,
            materialAsset != nullptr ? materialAsset->alphaCutoff : 0.5f
        };
        data.materialFlags =
            materialAsset != nullptr ? materialAsset->featureBits : 0u;
        if (materialAsset != nullptr && materialAsset->alphaMode == AlphaMode::Mask) {
            data.materialFlags |= MATERIAL_FEATURES::AlphaMask;
        }

        int baseColorHandle =
            RENDER3D::GetTextureResourceBackendHandle(baseColorTexture);
        if (source.materialOverride != nullptr) {
            data.baseColor = source.materialOverride->GetBaseColor();
            data.materialFlags = source.materialOverride->GetFeatureBits();
            if (source.materialOverride->HasBaseColorTexture()) {
                baseColorHandle =
                    source.materialOverride->GetBaseColorTextureHandle();
            }
        }
        if (baseColorHandle < 0) {
            baseColorHandle = gShadowRendererState.fallbackTextureHandle;
        }

        data.hasBaseColorTexture =
            baseColorHandle >= 0 && baseColorHandle != gShadowRendererState.fallbackTextureHandle ? 1u : 0u;
        data.hasNormalTexture = 0u;
        data.hasEmissiveTexture = 0u;
        data.hasMetallicRoughnessTexture = 0u;
        data.hasOcclusionTexture = 0u;
        data.normalScale = 1.0f;
        data.baseColorTextureHandle = baseColorHandle;
        data.normalTextureHandle = -1;
        data.emissiveTextureHandle = -1;
        data.metallicRoughnessTextureHandle = -1;
        data.occlusionTextureHandle = -1;
        data.baseColorTextureDescriptorIndex =
            ResolveTextureDescriptorIndex(baseColorHandle);
        data.normalTextureDescriptorIndex =
            MESHRENDERER::kInvalidTextureDescriptorIndex;
        data.emissiveTextureDescriptorIndex =
            MESHRENDERER::kInvalidTextureDescriptorIndex;
        data.metallicRoughnessTextureDescriptorIndex =
            MESHRENDERER::kInvalidTextureDescriptorIndex;
        data.occlusionTextureDescriptorIndex =
            MESHRENDERER::kInvalidTextureDescriptorIndex;
        return data;
    }

    void PrepareShadowSurfaceGpuSceneMaterialSources(
        uint32_t baseIndex,
        const std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource>* sources) {

        if (sources == nullptr || sources->empty()) {
            return;
        }

        for (size_t sourceIndex = 0; sourceIndex < sources->size(); ++sourceIndex) {
            const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source =
                (*sources)[sourceIndex];
            if (source.model == nullptr) {
                continue;
            }

            const MESHRENDERER::MaterialGpuData data =
                BuildShadowMaterialGpuData(source);
            const uint32_t materialDataIndex = UploadShadowMaterialData(
                BuildShadowMaterialDataKey(source.materialKey, data),
                data);
            gShadowRendererState.surfaceGpuSceneBuffer.PatchMaterialDataIndex(
                static_cast<size_t>(baseIndex) + sourceIndex,
                materialDataIndex == MESHRENDERER::kInvalidMaterialDataIndex
                    ? 0u
                    : materialDataIndex);
        }
    }

    void PrepareShadowSurfaceGpuSceneMaterialFrame() {
        const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& shadow =
            gShadowRendererState.activeShadowSceneSource.GetPass(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow);
        PrepareShadowSurfaceGpuSceneMaterialSources(
            shadow.gpuSceneBaseIndex,
            shadow.materialSources);
        PrepareShadowSurfaceGpuSceneMaterialSources(
            shadow.traditionalIndirect.gpuSceneBaseIndex,
            shadow.traditionalIndirect.materialSources);
    }

} // namespace HIKARI::SHADOW::INTERNAL
