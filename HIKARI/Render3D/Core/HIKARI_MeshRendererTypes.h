#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <DirectXMath.h>
#include <d3d12.h>

#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Render3D/Cluster/HIKARI_ClusterGpuCullingPass.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenLayer.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/HIKARI_Transform3D.h"
#include "Render3D/Material/HIKARI_GpuMaterialTypes.h"
#include "Render3D/Meshlet/HIKARI_MeshletRenderBackend.h"
#include "Render3D/Shadow/HIKARI_ShadowLightFrame.h"
#include "Vfx/Common/HIKARI_FxTypes.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

namespace HIKARI {
    class Material;
}

namespace HIKARI::MESHRENDERER {

    enum class MeshRenderDebugMode {
        Normal,
        WireOverlay,
        WireOnly,
    };

    struct MeshRendererFrameResult {
        bool frameRendered = false;
        bool massSubmissionRequired = false;
        bool massSubmissionCommitted = false;

        bool CanAcknowledgeMassRenderPublication() const noexcept {
            return frameRendered &&
                (!massSubmissionRequired || massSubmissionCommitted);
        }
    };

    struct CameraCB {
        MATH::Mat4 viewProj{};
        MATH::Mat4 invViewProj{};
        MATH::Vec4 cameraPos{};
        MATH::Vec4 timeParams{};
        MATH::Vec4 simulationTimeParams{};
        MATH::Vec4 screenParams{};
    };

    struct MeshFrameCameraOverrides {
        const MATH::Mat4* renderViewProj = nullptr;
        const MATH::Mat4* renderInvViewProj = nullptr;
        const MATH::Mat4* cullingViewProj = nullptr;
        const MATH::Mat4* cullingInvViewProj = nullptr;

        bool HasRenderMatrices() const {
            return renderViewProj != nullptr && renderInvViewProj != nullptr;
        }

        bool HasCullingMatrices() const {
            return cullingViewProj != nullptr && cullingInvViewProj != nullptr;
        }
    };

    struct ObjectCB {
        MATH::Mat4 world{};
        MATH::Mat4 normalMatrix{};
        MATH::Vec4 baseColor{};
        uint32_t hasBaseColorTexture = 0;
        uint32_t fxFlags = 0;
        uint32_t materialFlags = 0;
        float alphaCutoff = 0.5f;
        MATH::Vec4 emissiveFactor{};
        uint32_t hasNormalTexture = 0;
        float normalScale = 1.0f;
        float normalPadding[2]{};
        uint32_t receiveShadow = 1;
        float shadowObjectPadding[3]{};
        uint32_t hasEmissiveTexture = 0;
        float emissivePadding[3]{};
        float metallicFactor = 0.0f;
        float roughnessFactor = 1.0f;
        uint32_t hasMetallicRoughnessTexture = 0;
        uint32_t hasOcclusionTexture = 0;
        float occlusionStrength = 1.0f;
        float pbrPadding[3]{};
        MATH::Vec4 fxUser[VFX::kMaterialFxUserCount]{};
        MATH::Vec4 fxUser0{};
        MATH::Vec4 fxUser1{};
        MATH::Vec4 fxUser2{};
        MATH::Vec4 fxUser3{};
    };

    // StructuredBuffer layout. Keep this compact and independent from ObjectCB padding.
    struct ObjectGpuData {
        MATH::Mat4 world{};
        MATH::Mat4 normalMatrix{};
        MATH::Vec4 baseColor{};
        uint32_t hasBaseColorTexture = 0;
        uint32_t fxFlags = 0;
        uint32_t materialFlags = 0;
        float alphaCutoff = 0.5f;
        MATH::Vec4 emissiveFactor{};
        uint32_t hasNormalTexture = 0;
        float normalScale = 1.0f;
        float normalPadding[2]{};
        uint32_t receiveShadow = 1;
        float shadowObjectPadding[3]{};
        uint32_t hasEmissiveTexture = 0;
        float emissivePadding[3]{};
        float metallicFactor = 0.0f;
        float roughnessFactor = 1.0f;
        uint32_t hasMetallicRoughnessTexture = 0;
        uint32_t hasOcclusionTexture = 0;
        float occlusionStrength = 1.0f;
        uint32_t materialDataIndex = 0xffffffffu;
        float pbrPadding[2]{};
        MATH::Vec4 fxUser[VFX::kMaterialFxUserCount]{};
    };

    static_assert(sizeof(ObjectGpuData) == 384u);

    constexpr uint32_t kInvalidMaterialDataIndex =
        RENDER3D::MATERIAL::kInvalidGpuMaterialIndex;
    constexpr uint32_t kInvalidTextureDescriptorIndex =
        RENDER3D::MATERIAL::kInvalidTextureDescriptorIndex;
    using MaterialGpuData = RENDER3D::MATERIAL::GpuMaterialData;

    struct LightCB {
        MATH::Vec4 directionalDir{};
        MATH::Vec4 directionalColor{};
        MATH::Vec4 ambientColor{};
        MATH::Vec4 specularParams{};
        float directionalIntensity = 1.0f;
        float ambientIntensity = 0.25f;
        uint32_t localLightCount = 0;
        uint32_t localLightClusterCountX = 0u;
        uint32_t localLightClusterCountY = 0u;
        uint32_t localLightClusterCountZ = 0u;
        uint32_t localLightTileSizePixels = 64u;
        uint32_t localLightClusterIndexCount = 0u;
        float localLightNearDistance = 0.1f;
        float localLightFarDistance = 100.0f;
        float localLightDepthScale = 1.0f;
        float localLightDepthBias = 0.0f;
        MATH::Vec4 fogColorDensity{};
        MATH::Vec4 fogParams{};
        uint32_t debugView = 0;
        float forwardCostMode = 0.0f;
        float debugPadding[2]{};
        // View 単位の SSR 設定。Water も opaque provider と同じ authoring 値を使い、
        // 変形後の surface origin だけを差し替えて trace する。
        MATH::Vec4 screenSpaceReflectionParams0{};
        MATH::Vec4 screenSpaceReflectionParams1{};
    };

    static_assert(
        offsetof(LightCB, localLightDepthScale) == 104u);
    static_assert(
        offsetof(LightCB, fogColorDensity) == 112u);
    static_assert(
        offsetof(LightCB, screenSpaceReflectionParams0) == 160u);
    static_assert(sizeof(LightCB) == 192u);

    struct ReflectionProbeGpuData {
        MATH::Vec4 positionRadius{};
        MATH::Vec4 params{};
        MATH::Vec4 intensity{};
        MATH::Vec4 influenceBoxMin{};
        MATH::Vec4 influenceBoxMax{};
        MATH::Vec4 projectionBoxMin{};
        MATH::Vec4 projectionBoxMax{};
        MATH::Vec4 shapeParams{};
    };

    struct IrradianceVolumeGpuData {
        MATH::Vec4 originSampling{};
        MATH::Vec4 spacingIntensity{};
        MATH::Vec4 countsProbeCount{};
        MATH::Vec4 sizeBlendDistance{};
        MATH::Vec4 selectionParams{};
    };

    struct SkyEnvironmentCB {
        MATH::Vec4 skyZenithExposure{};
        MATH::Vec4 skyHorizonReflection{};
        MATH::Vec4 skyGroundAmbient{};
        MATH::Vec4 skyParams{};
        // rgb tint, w Texture2D user-SRV descriptor index plus one.
        MATH::Vec4 skyTintLighting{};
        MATH::Vec4 iblParams{};
        ReflectionProbeGpuData reflectionProbes[2]{};
        MATH::Vec4 ambientSamplingParams{};
        IrradianceVolumeGpuData irradianceVolumes[2]{};
        // Shared atmosphere source for Forward, water, and analytic IBL.
        MATH::Vec4 atmospherePlanetParams{};
        MATH::Vec4 atmosphereRayleigh{};
        MATH::Vec4 atmosphereMie{};
        MATH::Vec4 atmosphereOzone{};
        MATH::Vec4 atmosphereOzoneParams{};
        MATH::Vec4 atmosphereSunDirection{};
        MATH::Vec4 atmosphereSunColorRadiance{};
    };

    static_assert(
        offsetof(SkyEnvironmentCB, atmospherePlanetParams) == 528u);
    static_assert(sizeof(SkyEnvironmentCB) == 640u);

    struct ShadowCB {
        MATH::Mat4 lightViewProj[SHADOW::kMaxDirectionalShadowCascades]{};
        MATH::Vec4 cascadeSplitDistances{};
        MATH::Vec4 cascadeParams{};
        uint32_t enabled = 0;
        float depthBias = 0.001f;
        float normalBias = 0.02f;
        float strength = 0.75f;
        uint32_t pcfEnabled = 1;
        float pcfRadius = 1.0f;
        float texelSizeX = 1.0f / 2048.0f;
        float texelSizeY = 1.0f / 2048.0f;
        float edgeFade = 0.04f;
        float padding[3]{};
    };

    constexpr size_t kMaxJointPaletteMatrices = 128u;
    constexpr UINT kMaxObjectCount = 2048u;
    constexpr UINT kMaxMaterialDataCount =
        RENDER3D::MATERIAL::kDefaultGpuMaterialCapacity;

    struct JointPaletteCB {
        MATH::Mat4 jointMatrices[kMaxJointPaletteMatrices]{};
    };

    struct MaterialTextureDescriptorIndices {
        uint32_t baseColor = kInvalidTextureDescriptorIndex;
        uint32_t normal = kInvalidTextureDescriptorIndex;
        uint32_t emissive = kInvalidTextureDescriptorIndex;
        uint32_t metallicRoughness = kInvalidTextureDescriptorIndex;
        uint32_t occlusion = kInvalidTextureDescriptorIndex;
        uint32_t specular = kInvalidTextureDescriptorIndex;
        uint32_t specularColor = kInvalidTextureDescriptorIndex;
    };

    struct DrawItem {
        const ModelAsset* asset = nullptr;
        const Material* materialOverride = nullptr;
        Transform3D transform{};
        std::vector<MATH::Mat4> jointPalette{};
        std::string materialFxProfileId{};
        uint32_t postGroupMask = 0;
        VFX::VariantKey variant{};
        std::array<MATH::Vec4, VFX::kMaterialFxUserCount> fxValues{};
        uint32_t fxFlags = 0;
        std::array<DirectX::XMFLOAT4, VFX::kMaterialFxUserCount> materialFxParamValues{};
        bool materialFxValuesInitialized = false;
        bool hasResolvedMaterialFxProfile = false;
        MaterialFxProfile resolvedMaterialFxProfile{};
        bool usePrimitiveFilter = false;
        uint32_t meshIndexFilter = 0;
        uint32_t primitiveIndexFilter = 0;
        bool receiveShadow = true;
        MeshRenderDebugMode renderDebugMode = MeshRenderDebugMode::Normal;
    };

    inline bool IsDepthAwarePhaseItem(const DrawItem& item) {
        return item.hasResolvedMaterialFxProfile &&
            item.resolvedMaterialFxProfile.renderPhase == MaterialFxRenderPhase::DepthAware;
    }

    struct MeshRendererDebugStats {
        size_t skinnedGpuDrawCount = 0;
        size_t gpuDrivenSkinnedCommandCount = 0;
        size_t gpuDrivenSkinnedSourceRecordCount = 0;
        size_t uploadedJointCount = 0;
        size_t primitiveMeshCacheHitCount = 0;
        size_t primitiveMeshCacheMissCount = 0;
        size_t primitiveSkinnedMeshCacheHitCount = 0;
        size_t primitiveSkinnedMeshCacheMissCount = 0;
        size_t materialTextureCacheHitCount = 0;
        size_t materialTextureCacheMissCount = 0;
        size_t materialTextureLoadDeferredCount = 0;
        size_t psoCacheHitCount = 0;
        size_t psoCacheMissCount = 0;
        size_t materialFxProfileCacheHitCount = 0;
        size_t materialFxProfileCacheMissCount = 0;
        size_t materialFxProfileCacheFailCount = 0;
        size_t normalTextureCacheHitCount = 0;
        size_t normalTextureCacheMissCount = 0;
        size_t normalMappedPrimitiveCount = 0;
        size_t normalMapFallbackCount = 0;
        size_t emissiveTextureCacheHitCount = 0;
        size_t emissiveTextureCacheMissCount = 0;
        size_t emissiveMappedPrimitiveCount = 0;
        size_t emissiveMapFallbackCount = 0;
        RENDER3D::GPUDRIVEN::GpuTraditionalCommandStreamStats
            traditionalCommandStream{};
        RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadStats gpuSceneUpload{};
        size_t gpuDrivenCommandStreamGpuCommandCount = 0;
        size_t gpuDrivenCommandStreamTraditionalCommandCount = 0;
        RENDER3D::CLUSTER::ClusterGpuCullingPassStats clusterGpuCull{};
        size_t depthPrepassOccluderRecordCount = 0;
        size_t depthPrepassRejectedSmallRecordCount = 0;
        size_t depthPrepassRejectedUnsafeMaterialRecordCount = 0;
        RENDER3D::MESHLET::MeshletRenderBackendStats meshletBackend{};
        bool meshletDispatchArgumentBufferReady = false;
        bool meshletDispatchCommandSignatureReady = false;
        bool gpuDrivenCullingCameraFrozen = false;
        bool directionalEnabled = false;
        float directionalIntensity = 0.0f;
        float ambientIntensity = 0.0f;
        size_t localLightSourceCount = 0;
        size_t localLightUploadableCount = 0;
        size_t localLightUploadedCount = 0;
        size_t localLightClampedCount = 0;
        size_t pointLightUploadedCount = 0;
        size_t spotLightUploadedCount = 0;
        size_t localLightClusterCount = 0;
        size_t localLightClusterIndexCount = 0;
        size_t localLightClusterOverflowCount = 0;
        float specularIntensity = 0.0f;
        float specularPower = 0.0f;
    };

} // namespace HIKARI::MESHRENDERER
