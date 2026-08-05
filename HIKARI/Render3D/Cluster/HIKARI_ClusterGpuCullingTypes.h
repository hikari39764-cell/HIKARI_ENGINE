#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <d3d12.h>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::CLUSTER {

    constexpr size_t kDefaultClusterGpuCullingVisibleRangeCapacity = 524288u;
    constexpr size_t kDefaultClusterGpuCandidateCommandCapacity = 524288u;
    constexpr size_t kDefaultClusterGpuPageTaskCapacity = 262144u;

    enum class GeometryCullModeBucket : uint32_t {
        BackFace = 0,
        DoubleSided = 1,
        Count = 2,
    };

    constexpr size_t kGeometryCullModeBucketCount =
        static_cast<size_t>(GeometryCullModeBucket::Count);
    constexpr UINT64 kClusterGpuCullDrawCommandCounterOffsetBytes = 16u;
    constexpr UINT64 kClusterGpuCullDoubleSidedDrawCommandCounterOffsetBytes = 20u;

    enum class ClusterGpuCullingPassKind : uint32_t {
        ForwardOpaque = 0,
        ForwardDepthAware = 1,
        ForwardTransparent = 2,
        Shadow = 3,
        DepthPrepass = 4,
        Count = 5,
    };

    constexpr size_t kClusterGpuCullingPassKindCount =
        static_cast<size_t>(ClusterGpuCullingPassKind::Count);

    struct ClusterGpuCullingSourceRange {
        uint32_t surfaceGpuSceneBaseIndex = 0;
        uint32_t instanceCount = 0;
        ClusterGpuCullingPassKind passKind = ClusterGpuCullingPassKind::ForwardOpaque;
        // 0 / 0 は GPU 側で bucket 分類する主線を表す。
        uint32_t singleSidedInstanceCount = 0;
        uint32_t doubleSidedInstanceCount = 0;
    };

    struct ClusterGpuDepthOcclusionDesc {
        bool enabled = false;
        D3D12_GPU_DESCRIPTOR_HANDLE hzbSrv{};
        uint32_t hzbWidth = 0;
        uint32_t hzbHeight = 0;
        uint32_t hzbMipCount = 0;
        MATH::Mat4 hzbViewProj{};
        bool hzbViewProjValid = false;
    };

    struct ClusterGpuCullingPassStats {
        struct PassOutputStats {
            size_t sourceInstanceCount = 0;
            size_t sourceSingleSidedInstanceCount = 0;
            size_t sourceDoubleSidedInstanceCount = 0;
            size_t submittedDrawSeedCount = 0;
            size_t gpuBackFaceDrawCommandCount = 0;
            size_t gpuDoubleSidedDrawCommandCount = 0;
            size_t gpuDrawCommandCount = 0;
            size_t gpuBackFaceDrawCommandOverflowCount = 0;
            size_t gpuDoubleSidedDrawCommandOverflowCount = 0;
            size_t gpuDrawCommandOverflowCount = 0;
            bool gpuCounterReadbackValid = false;
        };

        bool initialized = false;
        bool psoReady = false;
        bool inputBufferReady = false;
        bool pageTaskBufferReady = false;
        bool visibleRangeBufferReady = false;
        bool visibleClusterListBufferReady = false;
        bool dispatchArgumentBufferReady = false;
        bool counterBufferReady = false;
        size_t inputCapacity = 0;
        size_t pageTaskCapacity = 0;
        size_t visibleRangeCapacity = 0;
        size_t visibleClusterListCapacity = 0;
        size_t candidateCommandCapacity = 0;
        size_t occlusionHistoryCapacity = 0;
        size_t sourceInstanceCount = 0;
        size_t sourceSingleSidedInstanceCount = 0;
        size_t sourceDoubleSidedInstanceCount = 0;
        std::array<PassOutputStats, kClusterGpuCullingPassKindCount> passOutputs{};
        size_t candidateInstanceCount = 0;
        size_t submittedInstanceCount = 0;
        size_t sourcePageTaskCount = 0;
        size_t submittedPageTaskCount = 0;
        size_t submittedDrawSeedCount = 0;
        size_t overflowInstanceCount = 0;
        bool gpuCounterReadbackReady = false;
        bool gpuCounterReadbackValid = false;
        bool debugCountersEnabled = false;
        bool meshletFineCullingDeferredToAmplificationShader = false;
        bool occlusionHistoryReady = false;
        uint32_t gpuInputCount = 0;
        uint32_t gpuPageTaskCount = 0;
        uint32_t gpuPageTaskOverflowCount = 0;
        uint32_t gpuVisibleRangeCount = 0;
        uint32_t gpuVisibleClusterCount = 0;
        uint32_t gpuOverflowCount = 0;
        bool hzbOcclusionEnabled = false;
        uint32_t hzbOcclusionWidth = 0;
        uint32_t hzbOcclusionHeight = 0;
        uint32_t hzbOcclusionMipCount = 0;
        uint32_t gpuInputFrustumCulledCount = 0;
        uint32_t gpuPageTestedCount = 0;
        uint32_t gpuPageFrustumCulledCount = 0;
        uint32_t gpuPageOcclusionTestedCount = 0;
        uint32_t gpuPageOcclusionCulledCount = 0;
        uint32_t gpuClusterTestedCount = 0;
        uint32_t gpuClusterFrustumCulledCount = 0;
        uint32_t gpuClusterOcclusionTestedCount = 0;
        uint32_t gpuClusterOcclusionCulledCount = 0;
        uint32_t gpuHzbPassRejectedCount = 0;
        uint32_t gpuHzbAabbRejectedCount = 0;
        uint32_t gpuHzbSphereRejectedCount = 0;
        uint32_t gpuHzbQueryAcceptedCount = 0;
        uint32_t gpuHzbTryCount = 0;
        uint32_t gpuHzbAllowedCount = 0;
        uint32_t gpuHzbInvalidRejectedCount = 0;
        uint32_t gpuHzbNearPlaneRejectedCount = 0;
        uint32_t gpuHzbOffscreenRejectedCount = 0;
        uint32_t gpuHzbLargeRectCount = 0;
        uint32_t gpuHzbAabbAcceptedCount = 0;
        uint32_t gpuHzbSphereAcceptedCount = 0;
        uint32_t gpuHzbRawOccludedCount = 0;
        uint32_t gpuHzbTemporalPendingCount = 0;
        uint32_t gpuHzbTemporalConfirmedCount = 0;
        uint32_t gpuHzbTemporalResetCount = 0;
        uint32_t gpuHzbTemporalCollisionCount = 0;
        uint32_t gpuHzbLargeRectSkippedCount = 0;
        uint32_t gpuPageHzbSmallScreenSkippedCount = 0;
        uint32_t gpuClusterHzbSmallScreenSkippedCount = 0;
        uint32_t gpuConeSkippedDoubleSidedCount = 0;
        uint32_t gpuConeSkippedMaterialCount = 0;
        uint32_t gpuClusterHzbLargeScreenSkippedCount = 0;
        uint32_t gpuHzbBudgetSkippedCount = 0;
        uint32_t gpuClusterConeCulledCount = 0;
        uint32_t gpuClusterConeTestedCount = 0;
        uint32_t gpuDoubleSidedClusterCount = 0;
        uint32_t gpuDrawCommandCount = 0;
        uint32_t gpuBackFaceDrawCommandCount = 0;
        uint32_t gpuDoubleSidedDrawCommandCount = 0;
        uint32_t gpuDrawCommandOverflowCount = 0;
        uint32_t gpuBackFaceDrawCommandOverflowCount = 0;
        uint32_t gpuDoubleSidedDrawCommandOverflowCount = 0;
        uint32_t gpuMergedGapCount = 0;
        uint32_t gpuMergedGapIndexCount = 0;
        uint32_t gpuPacketRangeCount = 0;
        uint32_t gpuPacketClusterCount = 0;
        uint32_t gpuVisibleClusterListReservedCount = 0;
        uint32_t gpuVisibleClusterListOverflowCount = 0;
        uint32_t gpuLod0SelectedCount = 0;
        uint32_t gpuLod1SelectedCount = 0;
        uint32_t gpuLod2SelectedCount = 0;
        uint32_t gpuLod3PlusSelectedCount = 0;
        float lodTargetErrorNdc = 0.0f;
        float lodTransitionRelaxPerLevel = 0.0f;
        float lodErrorRelaxPerLevel = 0.0f;
        size_t dispatchCount = 0;
        size_t workgroupCount = 0;
        uint32_t threadGroupSize = 64;

        bool IsReady() const noexcept {
            return initialized &&
                psoReady &&
                inputBufferReady &&
                visibleRangeBufferReady &&
                counterBufferReady;
        }
    };

} // namespace HIKARI::RENDER3D::CLUSTER
