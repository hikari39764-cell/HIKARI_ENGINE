#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <DirectXMath.h>
#include <d3d12.h>

#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/HIKARI_Transform3D.h"
#include "Render3D/Material/HIKARI_GpuMaterialTypes.h"
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

    struct CameraCB {
        MATH::Mat4 viewProj{};
        MATH::Mat4 invViewProj{};
        MATH::Vec4 cameraPos{};
        MATH::Vec4 timeParams{};
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
        MATH::Vec4 pointLightPosRange[8]{};
        MATH::Vec4 pointLightColorIntensity[8]{};
        float directionalIntensity = 1.0f;
        float ambientIntensity = 0.25f;
        uint32_t pointLightCount = 0;
        float lightPadding = 0.0f;
        MATH::Vec4 fogColorDensity{};
        MATH::Vec4 fogParams{};
        uint32_t debugView = 0;
        float forwardCostMode = 0.0f;
        float debugPadding[2]{};
    };

    struct SkyEnvironmentCB {
        MATH::Vec4 skyZenithExposure{};
        MATH::Vec4 skyHorizonReflection{};
        MATH::Vec4 skyGroundAmbient{};
        MATH::Vec4 skyParams{};
        MATH::Vec4 iblParams{};
        MATH::Vec4 reflectionProbePositionRadius{};
        MATH::Vec4 reflectionProbeParams{};
        MATH::Vec4 reflectionProbeIntensity{};
        MATH::Vec4 reflectionProbeInfluenceBoxMin{};
        MATH::Vec4 reflectionProbeInfluenceBoxMax{};
        MATH::Vec4 reflectionProbeProjectionBoxMin{};
        MATH::Vec4 reflectionProbeProjectionBoxMax{};
        MATH::Vec4 reflectionProbeShapeParams{};
        MATH::Vec4 aoParams{};
        MATH::Vec4 lightProbeVolumeOrigin{};
        MATH::Vec4 lightProbeVolumeSpacing{};
        MATH::Vec4 lightProbeVolumeCounts{};
    };

    struct ShadowCB {
        MATH::Mat4 lightViewProj{};
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
        size_t skinnedFallbackCount = 0;
        size_t gpuDrivenSkinnedCommandCount = 0;
        size_t gpuDrivenSkinnedSourceRecordCount = 0;
        size_t gpuDrivenSkinnedSubmittedRecordCount = 0;
        size_t gpuDrivenSkinnedSkippedRecordCount = 0;
        size_t uploadedJointCount = 0;
        size_t maxJointCount = 0;
        size_t lastSkinnedVertexCount = 0;
        size_t wireGpuDrawCount = 0;
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
        size_t pbrPrimitiveCount = 0;
        size_t unlitPrimitiveCount = 0;
        size_t metallicRoughnessTextureCacheHitCount = 0;
        size_t metallicRoughnessTextureCacheMissCount = 0;
        size_t metallicRoughnessMappedPrimitiveCount = 0;
        size_t metallicRoughnessFallbackCount = 0;
        size_t occlusionTextureCacheHitCount = 0;
        size_t occlusionTextureCacheMissCount = 0;
        size_t occlusionMappedPrimitiveCount = 0;
        size_t occlusionFallbackCount = 0;
        size_t rootSignatureBindCount = 0;
        size_t rootSignatureSkipCount = 0;
        size_t frameResourceBindCount = 0;
        size_t frameResourceSkipCount = 0;
        size_t objectResourceBindCount = 0;
        size_t objectResourceSkipCount = 0;
        size_t objectDataWriteCount = 0;
        size_t objectDataBufferBindCount = 0;
        size_t objectDataBufferSkipCount = 0;
        size_t objectIndexBindCount = 0;
        size_t objectIndexSkipCount = 0;
        size_t materialDataWriteCount = 0;
        size_t materialDataGpuUploadBytes = 0;
        size_t materialDataGpuUploadCallCount = 0;
        size_t materialDataCacheHitCount = 0;
        size_t materialDataCacheMissCount = 0;
        size_t materialDataOverflowCount = 0;
        size_t materialDataCachedCount = 0;
        size_t materialDataBufferBindCount = 0;
        size_t materialDataBufferSkipCount = 0;
        size_t materialIndexBindCount = 0;
        size_t materialIndexSkipCount = 0;
        size_t materialTexturePoolSlotCount = 0;
        size_t materialTexturePoolResolvedSlotCount = 0;
        size_t materialTexturePoolInvalidSlotCount = 0;
        size_t materialTexturePoolUniqueDescriptorCount = 0;
        size_t descriptorTableBindCount = 0;
        size_t descriptorTableSkipCount = 0;
        size_t pipelineStateBindCount = 0;
        size_t pipelineStateSkipCount = 0;
        size_t surfaceRecordExecutorRecordCount = 0;
        size_t surfaceRecordExecutorForwardDrawCount = 0;
        size_t surfaceRecordExecutorOpaqueDrawCount = 0;
        size_t surfaceRecordExecutorDepthAwareDrawCount = 0;
        size_t surfaceRecordExecutorTransparentDrawCount = 0;
        size_t surfaceRecordExecutorGeometryDrawCount = 0;
        size_t surfaceRecordExecutorSkippedRecordCount = 0;
        size_t surfaceRecordExecutorCommandCount = 0;
        size_t surfaceRecordExecutorOpaqueCommandCount = 0;
        size_t surfaceRecordExecutorDepthAwareCommandCount = 0;
        size_t surfaceRecordExecutorTransparentCommandCount = 0;
        size_t surfaceRecordExecutorSingleRecordCommandCount = 0;
        size_t surfaceRecordExecutorMergedCommandCount = 0;
        size_t surfaceRecordExecutorSavedCommandCount = 0;
        size_t surfaceRecordExecutorIndirectReadyCommandCount = 0;
        size_t surfaceRecordExecutorMissingDrawArgsCommandCount = 0;
        size_t surfaceRecordExecutorMaxCommandRecordCount = 0;
        size_t surfaceRecordExecutorInstancedDrawCount = 0;
        size_t surfaceRecordExecutorInstancedRecordCount = 0;
        size_t surfaceRecordExecutorMaxInstanceCount = 0;
        size_t surfaceRecordExecutorGpuSceneDrawCount = 0;
        size_t surfaceRecordExecutorGpuSceneRecordCount = 0;
        size_t surfaceGpuSceneCapacity = 0;
        size_t traditionalCommandStreamCommandCapacity = 0;
        size_t traditionalCommandStreamRequestedCommandCount = 0;
        size_t traditionalCommandStreamUploadedCommandCount = 0;
        size_t traditionalCommandStreamOverflowCommandCount = 0;
        size_t traditionalCommandStreamMissingDrawArgsCommandCount = 0;
        size_t traditionalCommandStreamUploadCallCount = 0;
        size_t traditionalCommandStreamInputUploadBytes = 0;
        size_t traditionalCommandStreamInputUploadCopyCount = 0;
        size_t traditionalCommandStreamResidentInputReuseCount = 0;
        size_t traditionalCommandStreamCommandStride = 0;
        size_t traditionalCommandStreamExecutedDrawCount = 0;
        size_t traditionalCommandStreamExecutedRecordCount = 0;
        size_t traditionalCommandStreamOpaqueCommandCount = 0;
        size_t traditionalCommandStreamOpaqueRecordCount = 0;
        size_t traditionalCommandStreamDepthAwareCommandCount = 0;
        size_t traditionalCommandStreamDepthAwareRecordCount = 0;
        size_t traditionalCommandStreamTransparentCommandCount = 0;
        size_t traditionalCommandStreamTransparentRecordCount = 0;
        size_t traditionalCommandStreamBatchSubmitCount = 0;
        size_t traditionalCommandStreamBatchedCommandCount = 0;
        size_t traditionalCommandStreamSavedSubmitCount = 0;
        size_t traditionalCommandStreamMaxBatchCommandCount = 0;
        size_t surfaceGpuSceneOpaqueInstanceCount = 0;
        size_t surfaceGpuSceneDepthPrepassInstanceCount = 0;
        size_t surfaceGpuSceneDepthAwareInstanceCount = 0;
        size_t surfaceGpuSceneTransparentInstanceCount = 0;
        size_t surfaceGpuSceneShadowInstanceCount = 0;
        size_t surfaceGpuSceneRequestedInstanceCount = 0;
        size_t surfaceGpuSceneUploadedInstanceCount = 0;
        size_t surfaceGpuSceneCommittedInstanceCount = 0;
        size_t surfaceGpuSceneCommittedBytes = 0;
        size_t surfaceGpuSceneFullUploadCount = 0;
        size_t surfaceGpuSceneDirtyPatchCount = 0;
        size_t surfaceGpuSceneReuseCount = 0;
        size_t surfaceGpuSceneOverflowInstanceCount = 0;
        size_t surfaceGpuSceneUploadCallCount = 0;
        size_t surfaceGpuSceneMaterialPatchCount = 0;
        size_t surfaceGpuSceneMaterialPatchChangedCount = 0;
        size_t surfaceGpuSceneMaterialPatchUnchangedCount = 0;
        size_t surfaceGpuSceneMaterialPatchFailCount = 0;
        size_t surfaceGpuSceneBufferBindCount = 0;
        size_t surfaceGpuSceneBufferSkipCount = 0;
        size_t gpuDrivenWorklistPassCount = 0;
        size_t gpuDrivenWorklistClusterPassCount = 0;
        size_t gpuDrivenWorklistSourceInstanceCount = 0;
        size_t gpuDrivenWorklistClusterInstanceCount = 0;
        size_t gpuDrivenCommandStreamPassCount = 0;
        size_t gpuDrivenCommandStreamRangeCount = 0;
        size_t gpuDrivenCommandStreamGpuCommandCount = 0;
        size_t gpuDrivenCommandStreamTraditionalCommandCount = 0;
        size_t gpuDrivenCommandStreamGpuCounterBackedRangeCount = 0;
        size_t gpuDrivenCommandStreamKnownVisibleCommandCount = 0;
        size_t gpuDrivenCommandStreamKnownVisibleCommandOverflowCount = 0;
        size_t clusterGpuCullSourceInstanceCount = 0;
        size_t clusterGpuCullCandidateInstanceCount = 0;
        size_t clusterGpuCullSubmittedInstanceCount = 0;
        size_t clusterGpuCullSourcePageTaskCount = 0;
        size_t clusterGpuCullSubmittedPageTaskCount = 0;
        size_t clusterGpuCullOverflowInstanceCount = 0;
        size_t clusterGpuCullGpuInputCount = 0;
        size_t clusterGpuCullGpuPageTaskCount = 0;
        size_t clusterGpuCullGpuPageTaskOverflowCount = 0;
        size_t clusterGpuCullGpuVisibleRangeCount = 0;
        size_t clusterGpuCullGpuVisibleClusterCount = 0;
        size_t clusterGpuCullGpuCulledInstanceCount = 0;
        size_t clusterGpuCullGpuOverflowCount = 0;
        size_t clusterGpuCullHzbOcclusionWidth = 0;
        size_t clusterGpuCullHzbOcclusionHeight = 0;
        size_t clusterGpuCullHzbOcclusionMipCount = 0;
        size_t clusterGpuCullGpuInputFrustumCulledCount = 0;
        size_t clusterGpuCullGpuPageTestedCount = 0;
        size_t clusterGpuCullGpuPageFrustumCulledCount = 0;
        size_t clusterGpuCullGpuPageOcclusionTestedCount = 0;
        size_t clusterGpuCullGpuPageOcclusionCulledCount = 0;
        size_t clusterGpuCullGpuClusterTestedCount = 0;
        size_t clusterGpuCullGpuClusterFrustumCulledCount = 0;
        size_t clusterGpuCullGpuClusterOcclusionTestedCount = 0;
        size_t clusterGpuCullGpuClusterOcclusionCulledCount = 0;
        size_t clusterGpuCullGpuHzbPassRejectedCount = 0;
        size_t clusterGpuCullGpuHzbAabbRejectedCount = 0;
        size_t clusterGpuCullGpuHzbSphereRejectedCount = 0;
        size_t clusterGpuCullGpuHzbQueryAcceptedCount = 0;
        size_t clusterGpuCullGpuHzbTryCount = 0;
        size_t clusterGpuCullGpuHzbAllowedCount = 0;
        size_t clusterGpuCullGpuHzbInvalidRejectedCount = 0;
        size_t clusterGpuCullGpuHzbNearPlaneRejectedCount = 0;
        size_t clusterGpuCullGpuHzbOffscreenRejectedCount = 0;
        size_t clusterGpuCullGpuHzbLargeRectCount = 0;
        size_t clusterGpuCullGpuHzbAabbAcceptedCount = 0;
        size_t clusterGpuCullGpuHzbSphereAcceptedCount = 0;
        size_t clusterGpuCullGpuHzbRawOccludedCount = 0;
        size_t clusterGpuCullGpuHzbTemporalPendingCount = 0;
        size_t clusterGpuCullGpuHzbTemporalConfirmedCount = 0;
        size_t clusterGpuCullGpuHzbTemporalResetCount = 0;
        size_t clusterGpuCullGpuHzbTemporalCollisionCount = 0;
        size_t clusterGpuCullGpuHzbLargeRectSkippedCount = 0;
        size_t clusterGpuCullGpuPageHzbSmallScreenSkippedCount = 0;
        size_t clusterGpuCullGpuClusterHzbSmallScreenSkippedCount = 0;
        size_t clusterGpuCullGpuConeSkippedDoubleSidedCount = 0;
        size_t clusterGpuCullGpuConeSkippedMaterialCount = 0;
        size_t clusterGpuCullGpuClusterHzbLargeScreenSkippedCount = 0;
        size_t clusterGpuCullGpuHzbBudgetSkippedCount = 0;
        size_t clusterGpuCullGpuClusterConeCulledCount = 0;
        size_t clusterGpuCullGpuClusterConeTestedCount = 0;
        size_t clusterGpuCullGpuDoubleSidedClusterCount = 0;
        size_t clusterGpuCullGpuDrawCommandCount = 0;
        size_t clusterGpuCullGpuBackFaceDrawCommandCount = 0;
        size_t clusterGpuCullGpuDoubleSidedDrawCommandCount = 0;
        size_t clusterGpuCullGpuDrawCommandOverflowCount = 0;
        size_t clusterGpuCullGpuBackFaceDrawCommandOverflowCount = 0;
        size_t clusterGpuCullGpuDoubleSidedDrawCommandOverflowCount = 0;
        size_t clusterGpuCullGpuMergedGapCount = 0;
        size_t clusterGpuCullGpuMergedGapIndexCount = 0;
        size_t clusterGpuCullGpuPacketRangeCount = 0;
        size_t clusterGpuCullGpuPacketClusterCount = 0;
        size_t clusterGpuCullGpuVisibleClusterListReservedCount = 0;
        size_t clusterGpuCullGpuVisibleClusterListOverflowCount = 0;
        size_t clusterGpuCullGpuLod0SelectedCount = 0;
        size_t clusterGpuCullGpuLod1SelectedCount = 0;
        size_t clusterGpuCullGpuLod2SelectedCount = 0;
        size_t clusterGpuCullGpuLod3PlusSelectedCount = 0;
        float clusterGpuCullLodTargetErrorNdc = 0.0f;
        float clusterGpuCullLodTransitionRelaxPerLevel = 0.0f;
        float clusterGpuCullLodErrorRelaxPerLevel = 0.0f;
        size_t depthPrepassOccluderRecordCount = 0;
        size_t depthPrepassRejectedSmallRecordCount = 0;
        size_t depthPrepassRejectedUnsafeMaterialRecordCount = 0;
        size_t clusterGpuCullDispatchCount = 0;
        size_t clusterGpuCullWorkgroupCount = 0;
        size_t clusterGpuCullInputCapacity = 0;
        size_t clusterGpuCullVisibleRangeCapacity = 0;
        size_t clusterGpuCullCandidateCommandCapacity = 0;
        size_t clusterGpuCullOcclusionHistoryCapacity = 0;
        size_t clusterGpuCullDrawSeedCount = 0;
        size_t meshletBackendRequestedDispatchCount = 0;
        size_t meshletBackendSubmittedDispatchCount = 0;
        size_t meshletBackendSkippedDispatchCount = 0;
        size_t meshletBackendSubmitCallCount = 0;
        size_t meshletBackendSkippedBucketCount = 0;
        size_t meshletBackendForwardSubmittedDispatchCount = 0;
        size_t meshletBackendGeometryAuxSubmittedDispatchCount = 0;
        size_t meshletBackendDepthPrepassSubmittedDispatchCount = 0;
        size_t meshletBackendShadowSubmittedDispatchCount = 0;
        size_t meshletBackendBackFaceSubmitCallCount = 0;
        size_t meshletBackendDoubleSidedSubmitCallCount = 0;
        size_t meshletBackendPipelineCreateRequestCount = 0;
        size_t meshletBackendPipelineCreateReadyCount = 0;
        uint32_t meshletBackendMeshShaderTier = 0;
        size_t clusterMainlineOwnedCommandCount = 0;
        size_t clusterMainlineOwnedRecordCount = 0;
        size_t clusterMainlineGeometryAuxCommandCount = 0;
        size_t clusterMainlineGeometryAuxRecordCount = 0;
        bool clusterMainlineReady = false;
        bool clusterMainlineForwardReady = false;
        bool clusterMainlineGeometryAuxReady = false;
        bool clusterMainlineHasDrawSeeds = false;
        bool clusterMainlineOverflowBlocked = false;
        bool surfaceGpuSceneSrvValid = false;
        bool surfaceGpuSceneBufferReady = false;
        bool traditionalCommandStreamArgumentBufferReady = false;
        bool traditionalCommandStreamCommandSignatureReady = false;
        bool clusterGpuCullReady = false;
        bool clusterGpuCullCounterReadbackReady = false;
        bool clusterGpuCullCounterReadbackValid = false;
        bool clusterGpuCullDebugCountersEnabled = false;
        bool clusterGpuCullFineCullingOwnedByAmplificationShader = false;
        bool clusterGpuCullHzbOcclusionEnabled = false;
        bool clusterGpuCullOcclusionHistoryReady = false;
        bool gpuDrivenCullingCameraFrozen = false;
        bool meshletBackendInitialized = false;
        bool meshletBackendShaderModel65Supported = false;
        bool meshletBackendMeshShaderSupported = false;
        bool meshletBackendPipelineStatsSupported = false;
        bool meshletBackendShaderCompileReady = false;
        bool meshletBackendDispatchArgumentBufferReady = false;
        bool meshletBackendDispatchCommandSignatureReady = false;
        bool meshletBackendForwardPipelineReady = false;
        bool meshletBackendGeometryAuxPipelineReady = false;
        bool meshletBackendPipelineReady = false;
        bool directionalEnabled = false;
        float directionalIntensity = 0.0f;
        float ambientIntensity = 0.0f;
        size_t pointLightTotalCount = 0;
        size_t pointLightUploadedCount = 0;
        size_t pointLightClampedCount = 0;
        float specularIntensity = 0.0f;
        float specularPower = 0.0f;
    };

} // namespace HIKARI::MESHRENDERER
