#pragma once

#include <cstdint>
#include <vector>

#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneSurfaceRecord.h"
#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    struct GpuSceneRegistrySyncInput {
        const RUNTIME::SceneRenderCache* sceneCache = nullptr;
    };

    struct GpuSceneStaticBatchStats {
        uint32_t recordCount = 0;
        uint32_t resourceBackedRecordCount = 0;
        uint32_t missingResourceHandleRecordCount = 0;
        uint32_t reorderedRecordCount = 0;
        uint32_t rawPsoRunCount = 0;
        uint32_t sortedPsoRunCount = 0;
        uint32_t rawMaterialRunCount = 0;
        uint32_t sortedMaterialRunCount = 0;
        uint32_t rawTextureSetRunCount = 0;
        uint32_t sortedTextureSetRunCount = 0;
        uint32_t rawGeometryRunCount = 0;
        uint32_t sortedGeometryRunCount = 0;
        uint32_t rawMeshResourceRunCount = 0;
        uint32_t sortedMeshResourceRunCount = 0;
        uint32_t rawMaterialResourceRunCount = 0;
        uint32_t sortedMaterialResourceRunCount = 0;
        uint32_t rawClusterResourceRunCount = 0;
        uint32_t sortedClusterResourceRunCount = 0;
        bool sortApplied = false;
    };

    struct GpuSceneRegistryStats {
        uint32_t sourceRecordCount = 0;
        uint32_t forwardRoutedRecordCount = 0;
        uint32_t forwardOpaqueResidentRecordCount = 0;
        uint32_t forwardOpaqueClusterCandidateRecordCount = 0;
        uint32_t unsupportedForwardRecordCount = 0;
        bool strictGpuDrivenMainline = false;
        uint32_t cpuForwardViewSuppressedCount = 0;
        uint32_t strictMainlineBlockedRecordCount = 0;
        uint32_t blockedForwardDepthAwareRecordCount = 0;
        uint32_t blockedForwardTransparentRecordCount = 0;
        uint32_t blockedShadowRecordCount = 0;
        uint32_t forwardStaticTraditionalRecordCount = 0;
        uint32_t shadowStaticTraditionalRecordCount = 0;
        uint32_t forwardMaterialFxMeshShaderRecordCount = 0;
        uint32_t forwardMaterialFxTraditionalRecordCount = 0;
        uint32_t forwardMaterialFxBlockedRecordCount = 0;
        uint32_t shadowMaterialFxMeshShaderRecordCount = 0;
        uint32_t shadowMaterialFxTraditionalRecordCount = 0;
        uint32_t forwardSkinnedMeshShaderRecordCount = 0;
        uint32_t shadowSkinnedMeshShaderRecordCount = 0;
        uint32_t forwardSkinnedTraditionalRecordCount = 0;
        uint32_t shadowSkinnedTraditionalRecordCount = 0;
        uint32_t depthPrepassOccluderRecordCount = 0;
        uint32_t depthPrepassRejectedSmallRecordCount = 0;
        uint32_t depthPrepassRejectedUnsafeMaterialRecordCount = 0;
        uint32_t depthPrepassBudgetClippedRecordCount = 0;

        RUNTIME::SurfaceGpuSceneBuildStats forwardOpaqueGpuSceneStats{};
        RUNTIME::SurfaceGpuSceneBuildStats depthPrepassGpuSceneStats{};
        RUNTIME::SurfaceGpuSceneBuildStats forwardDepthAwareGpuSceneStats{};
        RUNTIME::SurfaceGpuSceneBuildStats forwardTransparentGpuSceneStats{};
        RUNTIME::SurfaceGpuSceneBuildStats forwardOpaqueSkinnedTraditionalGpuSceneStats{};
        RUNTIME::SurfaceGpuSceneBuildStats forwardDepthAwareSkinnedTraditionalGpuSceneStats{};
        RUNTIME::SurfaceGpuSceneBuildStats forwardTransparentSkinnedTraditionalGpuSceneStats{};
        RUNTIME::SurfaceGpuSceneBuildStats shadowSkinnedTraditionalGpuSceneStats{};
        RUNTIME::SurfaceGpuSceneBuildStats forwardOpaqueTraditionalGpuSceneStats{};
        RUNTIME::SurfaceGpuSceneBuildStats forwardDepthAwareTraditionalGpuSceneStats{};
        RUNTIME::SurfaceGpuSceneBuildStats forwardTransparentTraditionalGpuSceneStats{};
        GpuSceneStaticBatchStats forwardOpaqueBatchStats{};
        GpuSceneStaticBatchStats forwardDepthAwareBatchStats{};
        GpuSceneStaticBatchStats forwardTransparentBatchStats{};
        GpuSceneStaticBatchStats shadowBatchStats{};
    };

    class GpuSceneRegistry final {
    public:
        void Clear();
        void SyncForwardFromSceneCache(const GpuSceneRegistrySyncInput& input);

        const GpuDrivenSceneSource& GetSceneSource() const;
        bool HasShadowPassSource() const;
        const std::vector<GpuSceneSurfaceRecord>& GetSurfaceRecords() const;
        const GpuSceneRegistryStats& GetStats() const;

    public:
        struct TraditionalIndirectStream {
            std::vector<GpuSceneSurfaceRecord> records{};
            std::vector<uint32_t> executableRecordIndices{};
            std::vector<RUNTIME::SurfaceDrawCommand> commands{};
            std::vector<RUNTIME::SurfaceGpuSceneInstance> instances{};
            std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> materialSources{};
            std::vector<std::vector<MATH::Mat4>> jointPalettes{};
            std::vector<VFX::VariantKey> bucketVariants{};
            uint32_t staticCommandCount = 0;
            uint32_t skinnedCommandCount = 0;

            void Clear();
            bool HasCommands() const;
        };

    private:
        void RebuildForwardFromSceneCache(const GpuSceneRegistrySyncInput& input);
        void SuppressCpuForwardViews(
            const GpuSceneRegistrySyncInput& input);
        bool TryPatchForwardDataFromSceneCache(const GpuSceneRegistrySyncInput& input);
        void RebuildForwardSceneSource();
        void ClearFrameDirtyRanges();

        std::vector<GpuSceneSurfaceRecord> surfaceRecords_{};
        std::vector<std::vector<MATH::Mat4>> meshShaderJointPalettes_{};
        std::vector<uint32_t> forwardOpaqueResidentRecordIndices_{};
        std::vector<uint32_t> depthPrepassOccluderRecordIndices_{};
        std::vector<uint32_t> forwardDepthAwareResidentRecordIndices_{};
        std::vector<uint32_t> forwardTransparentResidentRecordIndices_{};
        std::vector<uint32_t> shadowResidentRecordIndices_{};
        std::vector<uint32_t> forwardOpaqueSkinnedRecordIndices_{};
        std::vector<uint32_t> forwardDepthAwareSkinnedRecordIndices_{};
        std::vector<uint32_t> forwardTransparentSkinnedRecordIndices_{};
        std::vector<uint32_t> shadowSkinnedRecordIndices_{};
        std::vector<uint32_t> forwardOpaqueStaticTraditionalRecordIndices_{};
        std::vector<uint32_t> forwardDepthAwareStaticTraditionalRecordIndices_{};
        std::vector<uint32_t> forwardTransparentStaticTraditionalRecordIndices_{};
        std::vector<uint32_t> shadowStaticTraditionalRecordIndices_{};
        std::vector<uint32_t> forwardOpaqueGpuSceneIndexByRecord_{};
        std::vector<uint32_t> depthPrepassGpuSceneIndexByRecord_{};
        std::vector<uint32_t> forwardDepthAwareGpuSceneIndexByRecord_{};
        std::vector<uint32_t> forwardTransparentGpuSceneIndexByRecord_{};
        std::vector<uint32_t> shadowGpuSceneIndexByRecord_{};
        std::vector<uint32_t> globalGpuSceneIndexByRecord_{};
        std::vector<RUNTIME::SurfaceGpuSceneInstance> globalGpuSceneInstances_{};
        std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> globalMaterialSources_{};
        std::vector<RUNTIME::SurfaceGpuSceneInstance> forwardOpaqueGpuSceneInstances_{};
        std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> forwardOpaqueMaterialSources_{};
        std::vector<RUNTIME::SurfaceGpuSceneInstance> depthPrepassGpuSceneInstances_{};
        std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> depthPrepassMaterialSources_{};
        std::vector<RUNTIME::SurfaceGpuSceneInstance> forwardDepthAwareGpuSceneInstances_{};
        std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> forwardDepthAwareMaterialSources_{};
        std::vector<RUNTIME::SurfaceGpuSceneInstance> forwardTransparentGpuSceneInstances_{};
        std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> forwardTransparentMaterialSources_{};
        std::vector<RUNTIME::SurfaceGpuSceneInstance> shadowGpuSceneInstances_{};
        std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> shadowMaterialSources_{};
        TraditionalIndirectStream forwardOpaqueTraditionalStream_{};
        TraditionalIndirectStream forwardDepthAwareTraditionalStream_{};
        TraditionalIndirectStream forwardTransparentTraditionalStream_{};
        TraditionalIndirectStream shadowTraditionalStream_{};
        GpuDrivenSceneSource sceneSource_{};
        GpuSceneRegistryStats stats_{};
        uint64_t layoutVersion_ = 0;
        uint64_t routingVersion_ = 0;
        uint64_t dataVersion_ = 0;
        uint32_t sourceSurfaceCount_ = 0;
        bool publishStaticTraditionalStreams_ = false;
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
