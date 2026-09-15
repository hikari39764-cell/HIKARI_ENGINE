#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Render3D/Cluster/HIKARI_ClusterGpuCullingTypes.h"
#include "Render3D/Resources/HIKARI_RenderResourcePool.h"
#include "Render3D/Runtime/HIKARI_SurfaceGpuScene.h"

namespace HIKARI::RENDER3D::CLUSTER {

    class ClusterGpuCullingPass final {
    public:
        bool Initialize(
            const GFX::Context& graphics,
            ID3D12RootSignature* drawRootSignature,
            UINT rootConstantParameterIndex,
            UINT rootConstantCount,
            size_t pageTaskCapacity = kDefaultClusterGpuPageTaskCapacity,
            size_t visibleRangeCapacity = kDefaultClusterGpuCullingVisibleRangeCapacity,
            size_t candidateCommandCapacity = kDefaultClusterGpuCandidateCommandCapacity);
        void Reset();
        void BeginFrame(bool collectCounterReadback);

        bool Dispatch(
            ID3D12GraphicsCommandList* commandList,
            const MATH::Mat4& viewProj,
            const MATH::Vec3& cameraPosition,
            D3D12_GPU_DESCRIPTOR_HANDLE clusterGeometryPoolSrv,
            D3D12_GPU_VIRTUAL_ADDRESS surfaceGpuSceneGpuAddress,
            D3D12_GPU_VIRTUAL_ADDRESS massGpuSceneGpuAddress,
            const ClusterGpuCullingSourceRange* ranges,
            size_t rangeCount,
            const ClusterGpuDepthOcclusionDesc& depthOcclusion,
            bool collectCounterReadback = true);

        const ClusterGpuCullingPassStats& GetStats() const;
        const ClusterGpuCullingPassStats::PassOutputStats& GetPassStats(
            ClusterGpuCullingPassKind passKind) const;
        ID3D12Resource* GetVisibleRangeBuffer() const;
        ID3D12Resource* GetMeshletDispatchArgumentBuffer() const;
        ID3D12Resource* GetCounterBuffer() const;
        ID3D12CommandSignature* GetMeshletDispatchCommandSignature() const;
        size_t GetCandidateCommandBucketCapacity() const;
        UINT64 GetMeshletDispatchArgumentBufferOffset(
            ClusterGpuCullingPassKind passKind,
            GeometryCullModeBucket bucket) const;
        UINT64 GetCandidateCommandCounterOffset(
            ClusterGpuCullingPassKind passKind,
            GeometryCullModeBucket bucket) const;

    private:
        struct GpuVisibleRange {
            uint32_t gpuSceneInstanceIndex = 0;
            uint32_t clusterGeometrySrvDescriptorIndex = RUNTIME::kInvalidRenderSurfaceIndex;
            uint32_t firstCluster = RUNTIME::kInvalidRenderSurfaceIndex;
            uint32_t clusterCount = 0;
            uint32_t clusterSurfaceIndex = RUNTIME::kInvalidRenderSurfaceIndex;
            uint32_t passKind = 0;
            uint32_t flags = 0;
            uint32_t clusterGeometryMetadataSrvDescriptorIndex = RUNTIME::kInvalidRenderSurfaceIndex;
            uint32_t lodIndex = 0;
            uint32_t pageIndex = RUNTIME::kInvalidRenderSurfaceIndex;
            uint32_t drawBucket = 0;
            uint32_t sectionIndex = 0;
            uint32_t clusterOffsetBytes = 0;
            uint32_t vertexOffsetBytes = 0;
            uint32_t vertexCount = 0;
            uint32_t meshletPrimitiveOffsetBytes = 0;
            uint32_t meshletPrimitiveCount = 0;
            uint32_t geometryClusterCount = 0;
            uint32_t reserved0 = 0;
            uint32_t reserved1 = 0;
        };

        static_assert(sizeof(GpuVisibleRange) == 80u);

        struct GpuIndirectMeshletDispatchArgument {
            uint32_t rootConstants[4]{};
            D3D12_DISPATCH_MESH_ARGUMENTS dispatch{};
            uint32_t reserved0 = 0;
        };

        static_assert(sizeof(GpuIndirectMeshletDispatchArgument) == 32u);

        // HLSL 側 ClusterCullPageTask とレイアウト一致必須。clusterWorld は
        // GPU scene から task.gpuSceneInstanceIndex で読み直すため持たない。
        struct GpuPageTask {
            uint32_t gpuSceneInstanceIndex = RUNTIME::kInvalidRenderSurfaceIndex;
            uint32_t clusterGeometrySrvDescriptorIndex = RUNTIME::kInvalidRenderSurfaceIndex;
            uint32_t firstCluster = RUNTIME::kInvalidRenderSurfaceIndex;
            uint32_t endCluster = RUNTIME::kInvalidRenderSurfaceIndex;
            uint32_t clusterSurfaceIndex = RUNTIME::kInvalidRenderSurfaceIndex;
            uint32_t passKind = 0;
            uint32_t flags = 0;
            uint32_t pageIndex = RUNTIME::kInvalidRenderSurfaceIndex;
            uint32_t lodIndex = 0;
            uint32_t sectionIndex = 0;
            uint32_t clusterOffsetBytes = 0;
            uint32_t vertexOffsetBytes = 0;
            uint32_t vertexCount = 0;
            uint32_t meshletPrimitiveOffsetBytes = 0;
            uint32_t meshletPrimitiveCount = 0;
            uint32_t geometryClusterCount = 0;
            uint32_t reserved0 = 0;
            uint32_t reserved1 = 0;
            uint32_t reserved2 = 0;
            uint32_t clusterGeometryMetadataSrvDescriptorIndex = RUNTIME::kInvalidRenderSurfaceIndex;
        };

        static_assert(sizeof(GpuPageTask) == 80u);

        struct GpuCounters {
            uint32_t inputCount = 0;
            uint32_t visibleRangeCount = 0;
            uint32_t visibleClusterCount = 0;
            uint32_t overflowCount = 0;
            uint32_t backFaceDrawCommandCount = 0;
            uint32_t doubleSidedDrawCommandCount = 0;
            uint32_t backFaceDrawCommandOverflowCount = 0;
            uint32_t doubleSidedDrawCommandOverflowCount = 0;
            uint32_t inputFrustumCulledCount = 0;
            uint32_t pageTestedCount = 0;
            uint32_t pageFrustumCulledCount = 0;
            uint32_t clusterTestedCount = 0;
            uint32_t clusterFrustumCulledCount = 0;
            uint32_t clusterConeCulledCount = 0;
            uint32_t clusterConeTestedCount = 0;
            uint32_t doubleSidedClusterCount = 0;
            uint32_t pageTaskCount = 0;
            uint32_t pageTaskOverflowCount = 0;
            uint32_t mergedGapCount = 0;
            uint32_t mergedGapIndexCount = 0;
            uint32_t lod0SelectedCount = 0;
            uint32_t lod1SelectedCount = 0;
            uint32_t lod2SelectedCount = 0;
            uint32_t lod3PlusSelectedCount = 0;
            uint32_t pageOcclusionTestedCount = 0;
            uint32_t pageOcclusionCulledCount = 0;
            uint32_t clusterOcclusionTestedCount = 0;
            uint32_t clusterOcclusionCulledCount = 0;
            uint32_t hzbPassRejectedCount = 0;
            uint32_t hzbAabbRejectedCount = 0;
            uint32_t hzbSphereRejectedCount = 0;
            uint32_t hzbQueryAcceptedCount = 0;
            uint32_t hzbTryCount = 0;
            uint32_t hzbAllowedCount = 0;
            uint32_t hzbInvalidRejectedCount = 0;
            uint32_t hzbNearPlaneRejectedCount = 0;
            uint32_t hzbOffscreenRejectedCount = 0;
            uint32_t hzbLargeRectCount = 0;
            uint32_t hzbAabbAcceptedCount = 0;
            uint32_t hzbSphereAcceptedCount = 0;
            uint32_t hzbTemporalPendingCount = 0;
            uint32_t hzbTemporalConfirmedCount = 0;
            uint32_t hzbTemporalResetCount = 0;
            uint32_t hzbTemporalCollisionCount = 0;
            uint32_t hzbRawOccludedCount = 0;
            uint32_t hzbLargeRectSkippedCount = 0;
            uint32_t pageHzbSmallScreenSkippedCount = 0;
            uint32_t clusterHzbSmallScreenSkippedCount = 0;
            uint32_t coneSkippedDoubleSidedCount = 0;
            uint32_t coneSkippedMaterialCount = 0;
            uint32_t clusterHzbLargeScreenSkippedCount = 0;
            uint32_t hzbBudgetSkippedCount = 0;
            uint32_t packetRangeCount = 0;
            uint32_t packetClusterCount = 0;
            uint32_t reservedListCount = 0;
            uint32_t reservedListOverflow = 0;
        };

        static_assert(sizeof(GpuCounters) == 224u);

        struct GpuPassCounters {
            uint32_t backFaceDrawCommandCount = 0;
            uint32_t doubleSidedDrawCommandCount = 0;
            uint32_t backFaceDrawCommandOverflowCount = 0;
            uint32_t doubleSidedDrawCommandOverflowCount = 0;
        };

        static_assert(sizeof(GpuPassCounters) == 16u);

        struct GpuCounterBuffer {
            GpuCounters global{};
            std::array<GpuPassCounters, kClusterGpuCullingPassKindCount> passes{};
        };

        static_assert(offsetof(GpuCounterBuffer, passes) == 224u);
        static_assert(sizeof(GpuCounterBuffer) == 304u);

        struct CounterReadbackSlot {
            Microsoft::WRL::ComPtr<ID3D12Resource> buffer{};
            bool resolved = false;
        };

        struct GpuConstants {
            MATH::Mat4 viewProj{};
            MATH::Mat4 hzbViewProj{};
            MATH::Vec4 cameraPosition{};
            uint32_t inputCount = 0;
            uint32_t visibleRangeCapacity = 0;
            uint32_t candidateCommandCapacity = 0;
            uint32_t enableFrustumCull = 1;
            uint32_t candidateCommandBucketCapacity = 0;
            // Physical base/count of the dedicated HCMESH descriptor arena.
            uint32_t clusterSrvPoolBegin = 0;
            uint32_t clusterSrvPoolCount = 0;
            uint32_t enableConeCull = 1;
            uint32_t enableDebugCounters = 0;
            uint32_t surfaceGpuSceneBaseIndex = 0;
            uint32_t passKind = 0;
            uint32_t pageTaskCapacity = 0;
            uint32_t mergeGapIndexLimit = 0;
            uint32_t mergeRunGapIndexBudget = 0;
            uint32_t mergeMaxIndexSpan = 0;
            uint32_t mergeClusterGapLimit = 0;
            float lodTargetErrorNdc = 0.0f;
            uint32_t enableLodErrorSelection = 0;
            uint32_t pageTaskGroupSize = 1;
            uint32_t clusterHzbMinScreenPixels = 0;
            uint32_t enableHzbOcclusion = 0;
            uint32_t hzbWidth = 0;
            uint32_t hzbHeight = 0;
            uint32_t hzbMipCount = 0;
            float hzbDepthBias = 0.002f;
            float hzbMaxScreenRadiusPixels = 4096.0f;
            uint32_t occlusionHistoryCapacity = 0;
            uint32_t temporalFrameIndex = 0;
            uint32_t hzbOcclusionConfirmFrames = 2;
            uint32_t hzbAllowLargeRectOcclusion = 0;
            uint32_t hzbTestBudget = 0;
            uint32_t reservedVisibleListCapacity = 0;
            uint32_t meshletFineCullingOwner = 1;
            uint32_t candidateCommandReserved = 0;
            float lodTransitionRelaxPerLevel = 0.0f;
            float lodErrorRelaxPerLevel = 0.0f;
        };

        static_assert(sizeof(GpuConstants) == 288u);

        bool EnsurePipeline(ID3D12Device* device);
        bool EnsureDispatchCommandSignature(ID3D12Device* device);
        bool EnsureMeshletDispatchCommandSignature(
            ID3D12Device* device,
            ID3D12RootSignature* drawRootSignature,
            UINT rootConstantParameterIndex,
            UINT rootConstantCount);
        bool EnsureCapacity(
            ID3D12Device* device,
            size_t pageTaskCapacity,
            size_t visibleRangeCapacity,
            size_t candidateCommandCapacity);
        bool EnsureFallbackHzb(ID3D12Device* device);
        void ResetFrameStats();
        void BuildRangeStats(const ClusterGpuCullingSourceRange* ranges, size_t rangeCount);
        void CollectCounterReadback(CounterReadbackSlot& slot);
        void QueueCounterReadback(ID3D12GraphicsCommandList* commandList);
        void BindFrameResources(uint32_t frameIndex);
        void StoreActiveFrameResourceStates();

        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> expandPageTasksPipelineState_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> finalizeDispatchPipelineState_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> finalizeMeshletDispatchPipelineState_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> cullPageTasksPipelineState_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> transparentSortPipelineState_;
        Microsoft::WRL::ComPtr<ID3D12CommandSignature> dispatchCommandSignature_;
        Microsoft::WRL::ComPtr<ID3D12CommandSignature> meshletDispatchCommandSignature_;
        ID3D12DescriptorHeap* descriptorHeap_ = nullptr;
        struct FrameResources {
            Microsoft::WRL::ComPtr<ID3D12Resource> counterResetUploadBuffer;
            Microsoft::WRL::ComPtr<ID3D12Resource> pageTaskBuffer;
            Microsoft::WRL::ComPtr<ID3D12Resource> visibleRangeBuffer;
            Microsoft::WRL::ComPtr<ID3D12Resource> meshletDispatchArgumentBuffer;
            Microsoft::WRL::ComPtr<ID3D12Resource> dispatchArgumentBuffer;
            Microsoft::WRL::ComPtr<ID3D12Resource> counterBuffer;
            GpuCounterBuffer* counterResetMapped = nullptr;
            D3D12_RESOURCE_STATES pageTaskBufferState = D3D12_RESOURCE_STATE_COMMON;
            D3D12_RESOURCE_STATES visibleRangeBufferState = D3D12_RESOURCE_STATE_COMMON;
            D3D12_RESOURCE_STATES meshletDispatchArgumentBufferState = D3D12_RESOURCE_STATE_COMMON;
            D3D12_RESOURCE_STATES dispatchArgumentBufferState = D3D12_RESOURCE_STATE_COMMON;
            D3D12_RESOURCE_STATES counterBufferState = D3D12_RESOURCE_STATE_COMMON;
        };
        std::array<FrameResources, GFX::kFrameResourceCount> frameResources_{};
        std::array<DX::DynamicUploadBuffer, GFX::kFrameResourceCount> constantUploads_{};
        uint32_t activeFrameResourceIndex_ = 0;

        Microsoft::WRL::ComPtr<ID3D12Resource> counterResetUploadBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> pageTaskBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> visibleRangeBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> meshletDispatchArgumentBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> dispatchArgumentBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> counterBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> occlusionHistoryBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> fallbackHzb_;
        RenderResourceView fallbackHzbSrv_{};
        std::array<CounterReadbackSlot, 3> counterReadbackSlots_{};

        GpuCounterBuffer* counterResetMapped_ = nullptr;
        GpuCounterBuffer latestGpuCounters_{};
        size_t pageTaskCapacity_ = 0;
        size_t visibleRangeCapacity_ = 0;
        size_t candidateCommandCapacity_ = 0;
        size_t occlusionHistoryCapacity_ = 0;
        size_t counterReadbackWriteIndex_ = 0;
        bool latestGpuCountersValid_ = false;
        D3D12_RESOURCE_STATES pageTaskBufferState_ = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES visibleRangeBufferState_ = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES meshletDispatchArgumentBufferState_ = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES dispatchArgumentBufferState_ = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES counterBufferState_ = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES occlusionHistoryBufferState_ = D3D12_RESOURCE_STATE_COMMON;
        ClusterGpuCullingPassStats stats_{};
    };

} // namespace HIKARI::RENDER3D::CLUSTER
