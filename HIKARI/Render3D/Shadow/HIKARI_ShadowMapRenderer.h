#pragma once

#include <cstddef>
#include <cstdint>
#include <d3d12.h>

#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Shadow/HIKARI_ShadowCacheTypes.h"
#include "Render3D/HIKARI_Transform3D.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {
    struct GpuDrivenSceneSource;
}

namespace HIKARI::SHADOW {

    struct ShadowMapDebugStats {
        bool enabled = false;
        uint32_t resolution = 0;
        float worldTexelSize = 0.0f;
        size_t submittedCasterCount = 0;
        size_t shadowRecordCasterDrawCount = 0;
        size_t shadowRecordSkippedCount = 0;
        size_t shadowRecordCommandCount = 0;
        size_t shadowRecordSingleCommandCount = 0;
        size_t shadowRecordMaxCommandRecordCount = 0;
        size_t shadowRecordDrawCallCount = 0;
        size_t shadowRecordInstancedDrawCount = 0;
        size_t shadowRecordInstancedCasterCount = 0;
        size_t shadowRecordMaxInstanceCount = 0;
        size_t shadowIndirectCapacity = 0;
        size_t shadowIndirectRequestedCommandCount = 0;
        size_t shadowIndirectUploadedCommandCount = 0;
        size_t shadowIndirectOverflowCommandCount = 0;
        size_t shadowIndirectMissingDrawArgsCommandCount = 0;
        size_t shadowIndirectExecutedCommandCount = 0;
        size_t shadowIndirectExecutedRecordCount = 0;
        size_t shadowIndirectBatchSubmitCount = 0;
        size_t shadowIndirectSavedSubmitCount = 0;
        size_t shadowIndirectMaxBatchCommandCount = 0;
        bool shadowIndirectArgumentBufferReady = false;
        bool shadowIndirectCommandSignatureReady = false;
        size_t shadowGpuSceneCapacity = 0;
        size_t shadowGpuSceneRequestedInstanceCount = 0;
        size_t shadowGpuSceneUploadedInstanceCount = 0;
        size_t shadowGpuSceneOverflowInstanceCount = 0;
        size_t shadowGpuSceneUploadCallCount = 0;
        size_t shadowGpuSceneFullUploadCount = 0;
        size_t shadowGpuSceneDirtyPatchCount = 0;
        size_t shadowGpuSceneReuseCount = 0;
        bool shadowGpuSceneSrvValid = false;
        bool shadowGpuSceneBufferReady = false;
        size_t shadowMeshletRequestedDispatchCount = 0;
        size_t shadowMeshletSubmittedDispatchCount = 0;
        size_t shadowMeshletSkippedDispatchCount = 0;
        size_t shadowMeshletSubmitCallCount = 0;
        size_t shadowMeshletSkippedBucketCount = 0;
        size_t shadowMeshletBackFaceSubmitCallCount = 0;
        size_t shadowMeshletDoubleSidedSubmitCallCount = 0;
        bool shadowMeshletPipelineReady = false;
        bool shadowMeshletDispatchArgumentBufferReady = false;
        bool shadowMeshletDispatchCommandSignatureReady = false;
        bool shadowCacheValid = false;
        bool shadowCacheHit = false;
        uint32_t shadowCacheMissReasonFlags = ShadowCacheMissReasonNone;
        uint32_t shadowCacheLastMissReasonFlags = ShadowCacheMissReasonNone;
        size_t shadowCacheHitCount = 0;
        size_t shadowCacheMissCount = 0;
        size_t shadowStaticCacheCopyCount = 0;
        size_t shadowStaticCacheUpdateCount = 0;
        size_t shadowStaticSourceInstanceCount = 0;
        size_t shadowDynamicSourceInstanceCount = 0;
        bool shadowStaticRendered = false;
        bool shadowDynamicRendered = false;
        bool shadowUnifiedRendered = false;
        bool shadowFallbackRendered = false;
        size_t staticCasterDrawCount = 0;
        size_t skinnedCasterDrawCount = 0;
        size_t alphaMaskCasterDrawCount = 0;
        size_t skippedNoCastShadowCount = 0;
        size_t totalPrimitiveCasterDrawCount = 0;
        size_t shadowMapRecreateCount = 0;
        size_t pcfEnabled = 0;
        float pcfRadius = 0.0f;
        float orthoSize = 0.0f;
        float nearPlane = 0.0f;
        float farPlane = 0.0f;
        float depthBias = 0.0f;
        float normalBias = 0.0f;
        float strength = 0.0f;
    };

    void Reset();
    void BeginFrame(const SceneEnvironment& environment, const Camera3D& camera);
    void SetGpuDrivenSceneSource(
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* source);
    void InvalidateSceneCache();
    void RenderDirectionalShadowMap();

    bool IsDirectionalShadowEnabled();
    const MATH::Mat4& GetDirectionalLightViewProj();
    D3D12_GPU_DESCRIPTOR_HANDLE GetDirectionalShadowSrv();
    uint32_t GetShadowResolution();
    float GetShadowStrength();
    float GetDepthBias();
    float GetNormalBias();
    const ShadowMapDebugStats& GetDebugStats();

} // namespace HIKARI::SHADOW
