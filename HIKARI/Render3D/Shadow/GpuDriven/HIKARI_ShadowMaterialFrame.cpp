#include "Render3D/Shadow/Internal/HIKARI_ShadowRendererInternal.h"

#include <string>
#include <vector>

#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Material/HIKARI_GpuMaterialProjection.h"
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

        uint64_t seed = AppendShadowHashValue(HASH::kFnv1a64OffsetBasis, stableMaterialKey);
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
        using namespace RENDER3D::MATERIAL;
        const MaterialAsset* materialAsset = source.model != nullptr
            ? GetPrimitiveMaterial(*source.model, source.materialIndex) : nullptr;
        const RENDER3D::TextureResourceHandle baseColorTexture = source.model != nullptr
            ? ResolvePrimitiveTextureResource(*source.model, materialAsset)
            : gShadowRendererState.fallbackTextureResource;

        GpuMaterialProjection projection{};
        if (materialAsset != nullptr) {
            projection.baseColor = materialAsset->baseColorFactor;
            projection.pbrParams.w = materialAsset->alphaCutoff;
            projection.materialFlags = materialAsset->featureBits;
            if (materialAsset->alphaMode == AlphaMode::Mask)
                projection.materialFlags |= MATERIAL_FEATURES::AlphaMask;
        }
        int baseColorHandle = RENDER3D::GetTextureResourceBackendHandle(baseColorTexture);
        if (source.materialOverride != nullptr) {
            projection.baseColor = source.materialOverride->GetBaseColor();
            projection.pbrParams.w = source.materialOverride->GetAlphaCutoff();
            projection.materialFlags = source.materialOverride->GetFeatureBits();
            if (!source.materialOverride->InheritsModelTextures())
                baseColorHandle = gShadowRendererState.fallbackTextureHandle;
            const RuntimeTextureSlot& slot =
                source.materialOverride->GetTextureSlot(MaterialTextureUsage::BaseColor);
            if (slot.enabled) baseColorHandle = slot.ResolveBackendHandle();
        }
        if (baseColorHandle < 0) baseColorHandle = gShadowRendererState.fallbackTextureHandle;
        auto& baseColor = projection.textures[static_cast<size_t>(GpuMaterialTextureChannel::BaseColor)];
        baseColor.backendHandle = baseColorHandle;
        baseColor.descriptorIndex = ResolveTextureDescriptorIndex(baseColorHandle);
        baseColor.enabled = baseColorHandle >= 0 &&
            baseColorHandle != gShadowRendererState.fallbackTextureHandle &&
            baseColor.descriptorIndex != kInvalidTextureDescriptorIndex;
        ProjectMaterialTextureUv(
            materialAsset, source.materialOverride, MaterialTextureUsage::BaseColor, baseColor);
        return PackGpuMaterialData(projection);
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
