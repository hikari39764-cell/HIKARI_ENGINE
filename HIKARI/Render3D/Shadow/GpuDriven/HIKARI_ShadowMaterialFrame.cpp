#include "Render3D/Shadow/Internal/HIKARI_ShadowRendererInternal.h"

#include <string>
#include <vector>

#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Material/HIKARI_GpuMaterialProjection.h"
#include "Render3D/Material/Program/HIKARI_GpuMaterialProgramBinding.h"
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

    RENDER3D::TextureResourceHandle ResolvePrimitiveTextureResource(ShadowRendererState& state, const ModelAsset& asset, const MaterialAsset* materialAsset) {
        const TextureSlot* slot = GetModelMaterialTextureSlot(materialAsset, MaterialTextureUsage::BaseColor);
        if (slot == nullptr || slot->textureIndex < 0 ||
            static_cast<size_t>(slot->textureIndex) >= asset.textures.size()) {
            return state.fallbackTextureResource;
        }
        const auto& texturePath = GetMaterialTextureRuntimePath(asset.textures[static_cast<size_t>(slot->textureIndex)]);
        if (texturePath.empty()) return state.fallbackTextureResource;
        const std::string cacheKey = "shadow:base:" + texturePath;
        auto found = state.materialTextureCache.find(cacheKey);
        if (found != state.materialTextureCache.end() &&
            RENDER3D::IsTextureResourceValid(found->second)) {
            return found->second;
        }

        RENDER3D::TextureResourceHandle resource =
            RENDER3D::LoadTextureResourceWithColorSpace(cacheKey, texturePath,
                MaterialTextureColorSpace(MaterialTextureUsage::BaseColor));
        state.materialTextureCache[cacheKey] = resource;
        return RENDER3D::IsTextureResourceValid(resource) ? resource : state.fallbackTextureResource;
    }

    uint32_t ResolveTextureDescriptorIndex(int textureHandle) {
        const UINT descriptorIndex =
            RENDER3D::GetTextureResourceSrvDescriptorIndexFromBackendHandle(textureHandle);
        return descriptorIndex == UINT32_MAX
            ? MESHRENDERER::kInvalidTextureDescriptorIndex
            : static_cast<uint32_t>(descriptorIndex);
    }
    uint32_t UploadShadowMaterialData(
        ShadowRendererState& state, uint64_t stableMaterialKey,
        MESHRENDERER::MaterialGpuData data,
        const Material* programMaterial = nullptr) {

        using namespace RENDER3D::MATERIAL;
        auto& table = state.materialDataFrameTable;
        auto& materialData = GetActiveShadowFrameResources(state).materialDataStaging;
        if (materialData.empty()) {
            return MESHRENDERER::kInvalidMaterialDataIndex;
        }

        GpuMaterialProgramBinding binding{};
        std::span<const MATH::Vec4> parameters;
        std::vector<uint32_t> resources;
        const bool hasProgram = programMaterial != nullptr && programMaterial->GetMaterialProgramId();
        if (hasProgram) {
            if (!BuildGpuMaterialProgramBinding(*programMaterial, {}, binding)) {
                table.complete = false;
                return MESHRENDERER::kInvalidMaterialDataIndex;
            }
            parameters = programMaterial->GetMaterialProgramParameterBlock();
            resources = ResolveMaterialProgramResourceDescriptorIndices(*programMaterial);
            ApplyGpuMaterialProgramBinding(data, binding);
        }
        uint64_t key = AppendShadowHashValue(HASH::kFnv1a64OffsetBasis, stableMaterialKey);
        key = AppendShadowHashBytes(key, &data, sizeof(data));
        if (!parameters.empty()) key = AppendShadowHashBytes(key, parameters.data(), parameters.size_bytes());
        if (!resources.empty()) key = AppendShadowHashBytes(key, resources.data(), resources.size() * sizeof(uint32_t));
        const auto found = table.indexByKey.find(key);
        if (found != table.indexByKey.end()) {
            return found->second;
        }

        if (table.count >= MESHRENDERER::kMaxMaterialDataCount ||
            (hasProgram && !AppendGpuMaterialProgramBlock(data, binding, parameters, resources,
                table.programParameters, table.programResources))) {
            table.complete = false;
            return MESHRENDERER::kInvalidMaterialDataIndex;
        }

        const uint32_t index = table.count++;
        materialData[index] = data;
        table.indexByKey.emplace(key, index);
        return index;
    }

    void ResetShadowMaterialFrame(ShadowRendererState& state) {
        state.materialDataFrameTable.Clear();
        if (GetActiveShadowFrameResources(state).materialDataStaging.empty()) {
            return;
        }

        MESHRENDERER::MaterialGpuData defaultData{};
        defaultData.baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        defaultData.pbrParams = { 0.0f, 1.0f, 1.0f, 0.5f };
        defaultData.normalScale = 1.0f;
        defaultData.baseColorTextureHandle = state.fallbackTextureHandle;
        defaultData.normalTextureHandle = -1;
        defaultData.emissiveTextureHandle = -1;
        defaultData.metallicRoughnessTextureHandle = -1;
        defaultData.occlusionTextureHandle = -1;
        defaultData.baseColorTextureDescriptorIndex =
            ResolveTextureDescriptorIndex(state.fallbackTextureHandle);
        defaultData.normalTextureDescriptorIndex = MESHRENDERER::kInvalidTextureDescriptorIndex;
        defaultData.emissiveTextureDescriptorIndex = MESHRENDERER::kInvalidTextureDescriptorIndex;
        defaultData.metallicRoughnessTextureDescriptorIndex = MESHRENDERER::kInvalidTextureDescriptorIndex;
        defaultData.occlusionTextureDescriptorIndex = MESHRENDERER::kInvalidTextureDescriptorIndex;
        (void)UploadShadowMaterialData(state, 0u, defaultData);
    }

    MESHRENDERER::MaterialGpuData BuildShadowMaterialGpuData(
        ShadowRendererState& state, const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source) {
        using namespace RENDER3D::MATERIAL;
        const MaterialAsset* materialAsset = source.model != nullptr
            ? GetPrimitiveMaterial(*source.model, source.materialIndex) : nullptr;
        GpuMaterialProjection projection{BuildMaterialFactorProjection(materialAsset, source.materialOverride)};
        int baseColorHandle = state.fallbackTextureHandle;
        if (const auto* slot = ResolveMaterialTextureOverride(source.materialOverride, MaterialTextureUsage::BaseColor)) {
            if (slot->enabled) baseColorHandle = slot->ResolveBackendHandle();
        } else if (source.model != nullptr) {
            baseColorHandle = RENDER3D::GetTextureResourceBackendHandle(
                ResolvePrimitiveTextureResource(state, *source.model, materialAsset));
        }
        if (baseColorHandle < 0) baseColorHandle = state.fallbackTextureHandle;
        auto& baseColor = projection.textures[static_cast<size_t>(GpuMaterialTextureChannel::BaseColor)];
        baseColor.backendHandle = baseColorHandle;
        baseColor.descriptorIndex = ResolveTextureDescriptorIndex(baseColorHandle);
        baseColor.enabled = baseColorHandle >= 0 &&
            baseColorHandle != state.fallbackTextureHandle &&
            baseColor.descriptorIndex != kInvalidTextureDescriptorIndex;
        ProjectMaterialTextureUv(
            materialAsset, source.materialOverride, MaterialTextureUsage::BaseColor, baseColor);
        return PackGpuMaterialData(projection);
    }

    void PrepareShadowSurfaceGpuSceneMaterialSources(
        ShadowRendererState& state, uint32_t baseIndex,
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
                BuildShadowMaterialGpuData(state, source);
            const uint32_t materialDataIndex = UploadShadowMaterialData(
                state, source.materialKey, data, source.materialOverride);
            state.surfaceGpuSceneBuffer.PatchMaterialDataIndex(
                static_cast<size_t>(baseIndex) + sourceIndex,
                materialDataIndex == MESHRENDERER::kInvalidMaterialDataIndex
                    ? 0u
                    : materialDataIndex);
        }
    }

    void PrepareShadowSurfaceGpuSceneMaterialFrame(ShadowRendererState& state) {
        const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& shadow =
            state.activeShadowSceneSource.GetPass(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow);
        PrepareShadowSurfaceGpuSceneMaterialSources(
            state, shadow.gpuSceneBaseIndex,
            shadow.materialSources);
        PrepareShadowSurfaceGpuSceneMaterialSources(
            state, shadow.traditionalIndirect.gpuSceneBaseIndex,
            shadow.traditionalIndirect.materialSources);
    }

} // namespace HIKARI::SHADOW::INTERNAL
