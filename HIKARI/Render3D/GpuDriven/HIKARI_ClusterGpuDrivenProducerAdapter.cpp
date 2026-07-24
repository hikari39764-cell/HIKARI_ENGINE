#include "Render3D/GpuDriven/HIKARI_ClusterGpuDrivenProducerAdapter.h"

#include <array>

#include "Render3D/Cluster/HIKARI_ClusterGpuCullingPass.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    namespace {
        constexpr GpuDrivenCommandBucket ToGpuDrivenCommandBucket(
            CLUSTER::GeometryCullModeBucket bucket) {

            return bucket == CLUSTER::GeometryCullModeBucket::DoubleSided
                ? GpuDrivenCommandBucket::DoubleSided
                : GpuDrivenCommandBucket::BackFaceCulled;
        }

        bool TryToClusterCullPassKind(
            GpuDrivenPassKind passKind,
            CLUSTER::ClusterGpuCullingPassKind& outPassKind) {

            switch (passKind) {
            case GpuDrivenPassKind::ForwardDepthAware:
                outPassKind = CLUSTER::ClusterGpuCullingPassKind::ForwardDepthAware;
                return true;
            case GpuDrivenPassKind::ForwardTransparent:
                outPassKind = CLUSTER::ClusterGpuCullingPassKind::ForwardTransparent;
                return true;
            case GpuDrivenPassKind::Shadow:
                outPassKind = CLUSTER::ClusterGpuCullingPassKind::Shadow;
                return true;
            case GpuDrivenPassKind::DepthPrepass:
                outPassKind = CLUSTER::ClusterGpuCullingPassKind::DepthPrepass;
                return true;
            case GpuDrivenPassKind::ForwardOpaque:
                outPassKind = CLUSTER::ClusterGpuCullingPassKind::ForwardOpaque;
                return true;
            default:
                return false;
            }
        }

        void FillClusterPassOutput(
            GpuVisibilityResult& visibility,
            GpuCommandBuildResult& commands,
            GpuDrivenPassKind pass,
            CLUSTER::ClusterGpuCullingPassKind clusterPass,
            const CLUSTER::ClusterGpuCullingPass& cullingPass) {

            const CLUSTER::ClusterGpuCullingPassStats::PassOutputStats& stats =
                cullingPass.GetPassStats(clusterPass);
            GpuVisibilityPassResult& passVisibility =
                visibility.GetPass(pass);
            passVisibility.buckets[ToCommandBucketIndex(
                GpuDrivenCommandBucket::BackFaceCulled)].sourceInstanceCount =
                stats.sourceSingleSidedInstanceCount;
            passVisibility.buckets[ToCommandBucketIndex(
                GpuDrivenCommandBucket::DoubleSided)].sourceInstanceCount =
                stats.sourceDoubleSidedInstanceCount;
            passVisibility.submittedDrawSeedCount =
                stats.submittedDrawSeedCount;

            GpuDrivenCommandPassLayout& layout =
                commands.layout.GetPass(pass);
            layout.commandBucketCapacity =
                cullingPass.GetCandidateCommandBucketCapacity();
            constexpr CLUSTER::GeometryCullModeBucket sourceBuckets[] = {
                CLUSTER::GeometryCullModeBucket::BackFace,
                CLUSTER::GeometryCullModeBucket::DoubleSided,
            };
            for (const CLUSTER::GeometryCullModeBucket sourceBucket :
                sourceBuckets) {

                GpuDrivenCommandBucketLayout& bucketLayout =
                    layout.GetBucket(ToGpuDrivenCommandBucket(sourceBucket));
                GpuVisibilityBucketResult& bucketVisibility =
                    passVisibility.buckets[ToCommandBucketIndex(
                        ToGpuDrivenCommandBucket(sourceBucket))];
                bucketLayout.meshDispatchArgumentOffset =
                    cullingPass.GetMeshletDispatchArgumentBufferOffset(
                        clusterPass,
                        sourceBucket);
                bucketLayout.counterOffset =
                    cullingPass.GetCandidateCommandCounterOffset(
                        clusterPass,
                        sourceBucket);
                bucketVisibility.commandCapacity =
                    cullingPass.GetCandidateCommandBucketCapacity();
                bucketVisibility.commandCounterOffset =
                    bucketLayout.counterOffset;
                bucketVisibility.gpuCounterBacked =
                    bucketVisibility.commandCapacity != 0;
                bucketVisibility.visibleCommandCountKnown =
                    stats.gpuCounterReadbackValid;
                if (stats.gpuCounterReadbackValid) {
                    if (sourceBucket == CLUSTER::GeometryCullModeBucket::DoubleSided) {
                        bucketVisibility.visibleCommandCount =
                            stats.gpuDoubleSidedDrawCommandCount;
                        bucketVisibility.visibleCommandOverflowCount =
                            stats.gpuDoubleSidedDrawCommandOverflowCount;
                    } else {
                        bucketVisibility.visibleCommandCount =
                            stats.gpuBackFaceDrawCommandCount;
                        bucketVisibility.visibleCommandOverflowCount =
                            stats.gpuBackFaceDrawCommandOverflowCount;
                    }
                }
            }
        }
    }

    void ClusterGpuDrivenProducerAdapter::Attach(
        CLUSTER::ClusterGpuCullingPass* cullingPass) {

        cullingPass_ = cullingPass;
    }

    bool ClusterGpuDrivenProducerAdapter::IsAttached() const {
        return cullingPass_ != nullptr;
    }

    GpuDrivenProducerKind ClusterGpuDrivenProducerAdapter::GetProducerKind() const {
        return GpuDrivenProducerKind::ClusterGpuCulling;
    }

    void ClusterGpuDrivenProducerAdapter::BeginFrame(bool collectCounterReadback) {
        if (cullingPass_ != nullptr) {
            cullingPass_->BeginFrame(collectCounterReadback);
        }
    }

    GpuDrivenProducerFrameOutput ClusterGpuDrivenProducerAdapter::BuildFrameOutput() const {
        GpuDrivenProducerFrameOutput output{};
        output.producerKind = GetProducerKind();
        if (cullingPass_ == nullptr) {
            return output;
        }

        const CLUSTER::ClusterGpuCullingPassStats& stats = cullingPass_->GetStats();
        output.visibility.visibleInstanceBuffer = nullptr;
        output.visibility.visibleClusterRangeBuffer =
            cullingPass_->GetVisibleRangeBuffer();
        output.visibility.visibleMeshletRangeBuffer =
            cullingPass_->GetVisibleRangeBuffer();
        output.visibility.visibleMeshletClusterListBuffer =
            cullingPass_->GetVisibleClusterListBuffer();
        output.visibility.counterBuffer =
            cullingPass_->GetCounterBuffer();
        output.visibilitySeedCount =
            stats.submittedDrawSeedCount;
        output.visibilityOverflowInstanceCount =
            stats.overflowInstanceCount;
        output.visibilityReady =
            stats.initialized &&
            stats.visibleRangeBufferReady &&
            stats.visibleClusterListBufferReady &&
            stats.counterBufferReady;

        output.commands.meshDispatchArgs =
            cullingPass_->GetMeshletDispatchArgumentBuffer();
        output.commands.meshDispatchSignature =
            cullingPass_->GetMeshletDispatchCommandSignature();

        FillClusterPassOutput(
            output.visibility,
            output.commands,
            GpuDrivenPassKind::ForwardOpaque,
            CLUSTER::ClusterGpuCullingPassKind::ForwardOpaque,
            *cullingPass_);
        FillClusterPassOutput(
            output.visibility,
            output.commands,
            GpuDrivenPassKind::ForwardDepthAware,
            CLUSTER::ClusterGpuCullingPassKind::ForwardDepthAware,
            *cullingPass_);
        FillClusterPassOutput(
            output.visibility,
            output.commands,
            GpuDrivenPassKind::ForwardTransparent,
            CLUSTER::ClusterGpuCullingPassKind::ForwardTransparent,
            *cullingPass_);
        FillClusterPassOutput(
            output.visibility,
            output.commands,
            GpuDrivenPassKind::Shadow,
            CLUSTER::ClusterGpuCullingPassKind::Shadow,
            *cullingPass_);
        FillClusterPassOutput(
            output.visibility,
            output.commands,
            GpuDrivenPassKind::DepthPrepass,
            CLUSTER::ClusterGpuCullingPassKind::DepthPrepass,
            *cullingPass_);

        output.commandBuildReady =
            output.commands.meshDispatchArgs != nullptr;
        return output;
    }

    GpuDrivenProducerWorkResult ClusterGpuDrivenProducerAdapter::DispatchWork(
        const GpuDrivenProducerWorkContext& context) {

        GpuDrivenProducerWorkResult result{};
        if (cullingPass_ == nullptr) {
            return result;
        }

        std::array<CLUSTER::ClusterGpuCullingSourceRange, kGpuDrivenPassCount> ranges{};
        size_t rangeCount = 0;

        if (context.frame != nullptr) {
            for (const GpuDrivenPassFrame& pass : context.frame->passes) {
                if (!pass.HasSource() || !pass.clusterEligible) {
                    continue;
                }

                CLUSTER::ClusterGpuCullingPassKind clusterPass{};
                const GpuSceneRange& range = pass.gpuSceneRange;
                if ((context.passMask & MakeGpuDrivenPassMask(range.passKind)) == 0u) {
                    continue;
                }
                // DepthPrepass は occlusion 有効時も dispatch する。pyramid の
                // ソースとして使う場合の描画は finalize 前 (occlusion 無効の
                // BeginFrame ビルド) に行われ、finalize 後の scene depth prepass
                // は forward と同じ剔除結果を共有するのが正しい。
                if (!TryToClusterCullPassKind(range.passKind, clusterPass)) {
                    continue;
                }

                ranges[rangeCount] = {
                    range.baseIndex,
                    range.instanceCount,
                    clusterPass,
                    0u,
                    0u
                };
                ++rangeCount;
                ++result.sourcePassCount;
                result.sourceInstanceCount += range.instanceCount;
            }
        }

        CLUSTER::ClusterGpuDepthOcclusionDesc depthOcclusion{};
        depthOcclusion.enabled = context.depthOcclusion.enabled;
        depthOcclusion.hzbSrv = context.depthOcclusion.hzbSrv;
        depthOcclusion.hzbWidth = context.depthOcclusion.hzbWidth;
        depthOcclusion.hzbHeight = context.depthOcclusion.hzbHeight;
        depthOcclusion.hzbMipCount = context.depthOcclusion.hzbMipCount;
        depthOcclusion.hzbViewProj = context.depthOcclusion.hzbViewProj;
        depthOcclusion.hzbViewProjValid = context.depthOcclusion.hzbViewProjValid;

        // Traditional VS+PS は専用の GpuTraditionalCommandStreamBuffer が所有する。
        // Cluster producer は Mesh Shader 用の候補 Range / Dispatch だけを発行する。
        result.submitted = cullingPass_->Dispatch(
            context.commandList,
            context.viewProj,
            context.cameraPosition,
            context.geometryPoolSrv,
            context.surfaceGpuSceneGpuAddress,
            rangeCount != 0 ? ranges.data() : nullptr,
            rangeCount,
            depthOcclusion,
            context.collectCounterReadback);
        return result;
    }

    const CLUSTER::ClusterGpuCullingPassStats*
        ClusterGpuDrivenProducerAdapter::GetClusterStats() const {

        return cullingPass_ != nullptr ? &cullingPass_->GetStats() : nullptr;
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
