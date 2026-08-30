#include "Render3D/Pipeline/Internal/HIKARI_RenderFrameGraphStageInternal.h"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace HIKARI::RENDER3D::PIPELINE::INTERNAL {
    namespace {
        using ResourceUse = GRAPH::RenderGraphResourceUse;
        using MeshReadStorage = std::array<ResourceUse, 7u>;

        std::span<const ResourceUse> BuildMeshReads(
            const RenderFrameGraphTailResources& resources,
            GRAPH::RenderGraphResourceId dependency,
            bool includeOpaqueSceneColor,
            MeshReadStorage& storage) noexcept {
            std::size_t count = 0u;
            storage[count++] = ResourceUse{
                dependency, GRAPH::RenderGraphAccess::ReadDependency };
            storage[count++] = ResourceUse{
                resources.sceneDepth, GRAPH::RenderGraphAccess::ReadDepth };
            storage[count++] = ResourceUse{
                resources.packedSurface,
                GRAPH::RenderGraphAccess::ReadSampled };
            storage[count++] = ResourceUse{
                resources.ambientOcclusion,
                GRAPH::RenderGraphAccess::ReadSampled };
            storage[count++] = ResourceUse{
                resources.currentDepthPyramid,
                GRAPH::RenderGraphAccess::ReadSampled };
            storage[count++] = ResourceUse{
                resources.visibilityDepthPyramid,
                GRAPH::RenderGraphAccess::ReadSampled };
            if (includeOpaqueSceneColor) {
                storage[count++] = ResourceUse{
                    resources.sceneColorOpaque,
                    GRAPH::RenderGraphAccess::ReadSampled };
            }
            return std::span(storage.data(), count);
        }

        bool AddCompletionPass(
            GRAPH::RenderGraph& graph,
            std::string_view passId,
            std::string_view markerId,
            std::span<const ResourceUse> reads,
            GRAPH::RenderGraphExecuteFn execute,
            void* userData,
            GRAPH::RenderGraphResourceId& outMarker) noexcept {
            outMarker = AddRenderFrameGraphCompletionMarker(
                graph, markerId);
            if (!outMarker.IsValid()) {
                return false;
            }
            const std::array writes{
                ResourceUse{
                    outMarker,
                    GRAPH::RenderGraphAccess::WriteDependency
                }
            };
            return graph.AddPass(GRAPH::RenderGraphPassDesc{
                passId, reads, writes, execute, userData });
        }
    }

    bool BuildRenderFrameGraphTailPlan(
        const RenderFrameGraphPlanOptions& options,
        void* userData,
        const RenderFrameGraphPassCallbacks& callbacks,
        const RenderFrameGraphTailResources& resources,
        GRAPH::RenderGraph& graph) noexcept {
        if (!resources.completionDependency.IsValid() ||
            !resources.sceneDepth.IsValid() ||
            !resources.packedSurface.IsValid() ||
            !resources.ambientOcclusion.IsValid() ||
            !resources.currentDepthPyramid.IsValid() ||
            !resources.visibilityDepthPyramid.IsValid() ||
            !resources.surfaceFeatureMask.IsValid() ||
            !resources.stylizedOutlineStyle.IsValid() ||
            ((options.includeDepthAware || options.includeTransparent) &&
             !resources.sceneColorOpaque.IsValid())) {
            return false;
        }

        GRAPH::RenderGraphResourceId tailDependency =
            resources.completionDependency;
        MeshReadStorage meshReads{};
        if (options.includeDepthAware) {
            GRAPH::RenderGraphResourceId complete{};
            if (!AddCompletionPass(
                    graph,
                    "DepthAware",
                    "DepthAware.Complete",
                    BuildMeshReads(
                        resources, tailDependency, true, meshReads),
                    callbacks.depthAware,
                    userData,
                    complete)) {
                return false;
            }
            tailDependency = complete;
        }
        if (options.includeTransparent) {
            GRAPH::RenderGraphResourceId complete{};
            if (!AddCompletionPass(
                    graph,
                    "ForwardTransparent",
                    "ForwardTransparent.Complete",
                    BuildMeshReads(
                        resources, tailDependency, true, meshReads),
                    callbacks.transparent,
                    userData,
                    complete)) {
                return false;
            }
            tailDependency = complete;
        }

        const std::array featurePlanReads{
            ResourceUse{
                tailDependency,
                GRAPH::RenderGraphAccess::ReadDependency
            },
            ResourceUse{
                resources.sceneDepth,
                GRAPH::RenderGraphAccess::ReadSampled
            },
            ResourceUse{
                resources.packedSurface,
                GRAPH::RenderGraphAccess::ReadSampled
            },
            ResourceUse{
                resources.surfaceFeatureMask,
                GRAPH::RenderGraphAccess::ReadSampled
            }
        };
        GRAPH::RenderGraphResourceId featurePlanComplete{};
        if (!AddCompletionPass(
                graph,
                "RenderFeatures.Plan",
                "RenderFeatures.Plan.Complete",
                featurePlanReads,
                callbacks.featurePlan,
                userData,
                featurePlanComplete)) {
            return false;
        }

        GRAPH::RenderGraphResourceId geometryComplete{};
        if (!AddCompletionPass(
                graph,
                "RenderFeatures.Geometry",
                "RenderFeatures.Geometry.Complete",
                BuildMeshReads(
                    resources,
                    featurePlanComplete,
                    resources.sceneColorOpaque.IsValid(),
                    meshReads),
                callbacks.geometryFeatures,
                userData,
                geometryComplete)) {
            return false;
        }
        GRAPH::RenderGraphResourceId visibilityComplete{};
        if (!AddCompletionPass(
                graph,
                "RenderFeatures.Visibility",
                "RenderFeatures.Visibility.Complete",
                BuildMeshReads(
                    resources,
                    geometryComplete,
                    resources.sceneColorOpaque.IsValid(),
                    meshReads),
                callbacks.visibilityFeatures,
                userData,
                visibilityComplete)) {
            return false;
        }

        const auto sceneColorPostOpaque =
            AddRenderFrameGraphSurfaceResource(
                graph,
                "SceneColorPostOpaque",
                GRAPH::RenderGraphSemantic::SceneColorPostOpaque,
                false,
                GRAPH::RenderGraphSemanticPhase::PostOpaque,
                SURFACE::SceneSurfaceProducer::SceneColorCapture,
                GRAPH::RenderGraphResourceRequirement::Optional);
        const auto postOpaqueCaptureComplete =
            AddRenderFrameGraphCompletionMarker(
                graph, "SceneColorPostOpaque.Capture.Complete");
        if (!sceneColorPostOpaque.IsValid() ||
            !postOpaqueCaptureComplete.IsValid()) {
            return false;
        }
        const std::array captureReads{
            ResourceUse{
                visibilityComplete,
                GRAPH::RenderGraphAccess::ReadDependency
            }
        };
        const std::array captureWrites{
            ResourceUse{
                sceneColorPostOpaque,
                GRAPH::RenderGraphAccess::CopyDestination,
                SURFACE::SceneSurfaceProducer::SceneColorCapture
            },
            ResourceUse{
                postOpaqueCaptureComplete,
                GRAPH::RenderGraphAccess::WriteDependency
            }
        };
        if (!graph.AddPass(GRAPH::RenderGraphPassDesc{
                "SceneColorPostOpaque.Capture",
                captureReads,
                captureWrites,
                callbacks.sceneColorPostOpaqueCapture,
                userData
            })) {
            return false;
        }

        const std::array compositeReads{
            ResourceUse{
                postOpaqueCaptureComplete,
                GRAPH::RenderGraphAccess::ReadDependency
            },
            ResourceUse{
                sceneColorPostOpaque,
                GRAPH::RenderGraphAccess::ReadSampled
            },
            ResourceUse{
                resources.sceneDepth,
                GRAPH::RenderGraphAccess::ReadSampled
            },
            ResourceUse{
                resources.packedSurface,
                GRAPH::RenderGraphAccess::ReadSampled
            },
            ResourceUse{
                resources.surfaceFeatureMask,
                GRAPH::RenderGraphAccess::ReadSampled
            },
            ResourceUse{
                resources.stylizedOutlineStyle,
                GRAPH::RenderGraphAccess::ReadSampled
            }
        };
        GRAPH::RenderGraphResourceId compositeComplete{};
        return AddCompletionPass(
            graph,
            "RenderFeatures.Composite",
            "RenderFeatures.Composite.Complete",
            compositeReads,
            callbacks.stylizedFeatureComposite,
            userData,
            compositeComplete);
    }

} // namespace HIKARI::RENDER3D::PIPELINE::INTERNAL
