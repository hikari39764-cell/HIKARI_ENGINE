#include "Render3D/Pipeline/Internal/HIKARI_RenderFrameGraphStageInternal.h"

#include <array>

#include "Render3D/Graph/HIKARI_RenderGraphSurfaceContract.h"

namespace HIKARI::RENDER3D::PIPELINE::INTERNAL {
    namespace {
        GRAPH::RenderGraphResourceId AddSurfaceResource(
            GRAPH::RenderGraph& graph,
            std::string_view id,
            GRAPH::RenderGraphSemantic semantic,
            bool imported,
            GRAPH::RenderGraphSemanticPhase phase =
                GRAPH::RenderGraphSemanticPhase::None,
            SURFACE::SceneSurfaceProducer producer =
                SURFACE::SceneSurfaceProducer::None,
            GRAPH::RenderGraphResourceRequirement requirement =
                GRAPH::RenderGraphResourceRequirement::Required) noexcept {
            GRAPH::RenderGraphResourceDesc desc{};
            desc.id = id;
            desc.semantic = semantic;
            desc.imported = imported;
            desc.kind = GRAPH::RenderGraphResourceKind::SceneSurface;
            desc.phase = phase;
            desc.requirement = requirement;
            desc.expectedProducer = producer;
            return graph.AddResource(desc);
        }

        GRAPH::RenderGraphResourceId AddCompletionMarker(
            GRAPH::RenderGraph& graph,
            std::string_view id) noexcept {
            GRAPH::RenderGraphResourceDesc desc{};
            desc.id = id;
            desc.kind = GRAPH::RenderGraphResourceKind::CompletionMarker;
            return graph.AddResource(desc);
        }
    }

    bool BuildRenderFrameGraphPlan(
        const RenderFrameGraphPlanOptions& options,
        void* userData,
        const RenderFrameGraphPassCallbacks& callbacks,
        GRAPH::RenderGraph& graph) noexcept {
        const auto sceneDepth = AddSurfaceResource(
            graph,
            "SceneDepth",
            GRAPH::RenderGraphSemantic::SceneDepth,
            true,
            GRAPH::RenderGraphSemanticPhase::Imported,
            SURFACE::SceneSurfaceProducer::PostSceneDepth);
        const auto packedSurfaceInput = AddSurfaceResource(
            graph,
            "PackedSurface.PreOpaque",
            GRAPH::RenderGraphSemantic::PackedSurface,
            true,
            GRAPH::RenderGraphSemanticPhase::PreOpaque,
            SURFACE::SceneSurfaceProducer::SceneSurfaceAux,
            GRAPH::RenderGraphResourceRequirement::Optional);
        const auto packedSurface = AddSurfaceResource(
            graph,
            "PackedSurface",
            GRAPH::RenderGraphSemantic::PackedSurface,
            false,
            GRAPH::RenderGraphSemanticPhase::PostOpaque,
            SURFACE::SceneSurfaceProducer::SceneSurfaceAux,
            GRAPH::RenderGraphResourceRequirement::Optional);
        const auto currentDepthPyramid = AddSurfaceResource(
            graph,
            "CurrentDepthPyramid",
            GRAPH::RenderGraphSemantic::CurrentDepthPyramid,
            false,
            GRAPH::RenderGraphSemanticPhase::PostOpaque,
            SURFACE::SceneSurfaceProducer::DepthPyramid,
            GRAPH::RenderGraphResourceRequirement::Optional);
        const auto forwardDepthPyramid = AddSurfaceResource(
            graph,
            "DepthPyramid.Forward",
            GRAPH::RenderGraphSemantic::VisibilityDepthPyramid,
            true,
            GRAPH::RenderGraphSemanticPhase::PreOpaque,
            SURFACE::SceneSurfaceProducer::DepthPyramid,
            GRAPH::RenderGraphResourceRequirement::Optional);
        const auto ambientOcclusion = AddSurfaceResource(
            graph,
            "AmbientOcclusion.Forward",
            GRAPH::RenderGraphSemantic::AmbientOcclusion,
            true,
            GRAPH::RenderGraphSemanticPhase::PreOpaque,
            SURFACE::SceneSurfaceProducer::Ssao,
            GRAPH::RenderGraphResourceRequirement::Optional);
        const auto surfaceFeatureMask = AddSurfaceResource(
            graph,
            "SurfaceFeatureMask",
            GRAPH::RenderGraphSemantic::SurfaceFeatureMask,
            false,
            GRAPH::RenderGraphSemanticPhase::PostOpaque,
            SURFACE::SceneSurfaceProducer::SceneSurfaceAux,
            GRAPH::RenderGraphResourceRequirement::Optional);
        const auto stylizedOutlineStyle = AddSurfaceResource(
            graph,
            "StylizedOutlineStyle",
            GRAPH::RenderGraphSemantic::StylizedOutlineStyle,
            false,
            GRAPH::RenderGraphSemanticPhase::PostOpaque,
            SURFACE::SceneSurfaceProducer::SceneSurfaceAux,
            GRAPH::RenderGraphResourceRequirement::Optional);
        const auto forwardComplete = AddCompletionMarker(
            graph, "ForwardOpaque.Complete");
        const auto postOpaqueComplete = AddCompletionMarker(
            graph, "PostOpaqueSurfaces.Complete");
        const auto cloudsComplete = AddCompletionMarker(
            graph, "Clouds.Complete");
        const auto motionComplete = AddCompletionMarker(
            graph, "MotionVectors.Complete");
        const auto motionVectors = AddSurfaceResource(
            graph,
            "MotionVectors",
            GRAPH::RenderGraphSemantic::MotionVectors,
            false,
            GRAPH::RenderGraphSemanticPhase::Temporal,
            SURFACE::SceneSurfaceProducer::TemporalMotion);
        const auto motionMetadata = AddSurfaceResource(
            graph,
            "MotionMetadata",
            GRAPH::RenderGraphSemantic::MotionMetadata,
            false,
            GRAPH::RenderGraphSemanticPhase::Temporal,
            SURFACE::SceneSurfaceProducer::TemporalMotion);
        if (!sceneDepth.IsValid() ||
            !packedSurfaceInput.IsValid() ||
            !packedSurface.IsValid() ||
            !currentDepthPyramid.IsValid() ||
            !forwardDepthPyramid.IsValid() ||
            !ambientOcclusion.IsValid() ||
            !surfaceFeatureMask.IsValid() ||
            !stylizedOutlineStyle.IsValid() ||
            !forwardComplete.IsValid() ||
            !postOpaqueComplete.IsValid() ||
            !cloudsComplete.IsValid() ||
            !motionComplete.IsValid() ||
            !motionVectors.IsValid() ||
            !motionMetadata.IsValid()) {
            return false;
        }

        const std::array forwardReads{
            GRAPH::RenderGraphResourceUse{
                sceneDepth,
                GRAPH::RenderGraphAccess::ReadDepth
            },
            GRAPH::RenderGraphResourceUse{
                packedSurfaceInput,
                GRAPH::RenderGraphAccess::ReadSampled
            },
            GRAPH::RenderGraphResourceUse{
                forwardDepthPyramid,
                GRAPH::RenderGraphAccess::ReadSampled
            },
            GRAPH::RenderGraphResourceUse{
                ambientOcclusion,
                GRAPH::RenderGraphAccess::ReadSampled
            }
        };
        const std::array forwardWrites{
            GRAPH::RenderGraphResourceUse{
                forwardComplete,
                GRAPH::RenderGraphAccess::WriteDependency
            }
        };
        if (!graph.AddPass(GRAPH::RenderGraphPassDesc{
                "ForwardOpaque",
                forwardReads,
                forwardWrites,
                callbacks.forwardOpaque,
                userData
            })) {
            return false;
        }

        const std::array postReads{
            GRAPH::RenderGraphResourceUse{
                forwardComplete,
                GRAPH::RenderGraphAccess::ReadDependency
            },
            GRAPH::RenderGraphResourceUse{
                sceneDepth,
                GRAPH::RenderGraphAccess::ReadDepth
            }
        };
        const std::array postWrites{
            GRAPH::RenderGraphResourceUse{
                postOpaqueComplete,
                GRAPH::RenderGraphAccess::WriteDependency
            },
            GRAPH::RenderGraphResourceUse{
                packedSurface,
                GRAPH::RenderGraphAccess::WriteColor,
                SURFACE::SceneSurfaceProducer::SceneSurfaceAux
            },
            GRAPH::RenderGraphResourceUse{
                currentDepthPyramid,
                GRAPH::RenderGraphAccess::WriteUav,
                SURFACE::SceneSurfaceProducer::DepthPyramid
            },
            GRAPH::RenderGraphResourceUse{
                surfaceFeatureMask,
                GRAPH::RenderGraphAccess::WriteColor,
                SURFACE::SceneSurfaceProducer::SceneSurfaceAux
            },
            GRAPH::RenderGraphResourceUse{
                stylizedOutlineStyle,
                GRAPH::RenderGraphAccess::WriteColor,
                SURFACE::SceneSurfaceProducer::SceneSurfaceAux
            }
        };
        if (!graph.AddPass(GRAPH::RenderGraphPassDesc{
                "ScreenSpacePostOpaque",
                postReads,
                postWrites,
                callbacks.postOpaqueSurfaces,
                userData
            })) {
            return false;
        }

        const std::array cloudReads{
            GRAPH::RenderGraphResourceUse{
                postOpaqueComplete,
                GRAPH::RenderGraphAccess::ReadDependency
            },
            GRAPH::RenderGraphResourceUse{
                sceneDepth,
                GRAPH::RenderGraphAccess::ReadDepth
            }
        };
        const std::array cloudWrites{
            GRAPH::RenderGraphResourceUse{
                cloudsComplete,
                GRAPH::RenderGraphAccess::WriteDependency
            }
        };
        if (!graph.AddPass(GRAPH::RenderGraphPassDesc{
                "StylizedClouds",
                cloudReads,
                cloudWrites,
                callbacks.clouds,
                userData
            })) {
            return false;
        }

        const std::array motionReads{
            GRAPH::RenderGraphResourceUse{
                sceneDepth,
                GRAPH::RenderGraphAccess::ReadDepth
            },
            GRAPH::RenderGraphResourceUse{
                cloudsComplete,
                GRAPH::RenderGraphAccess::ReadDependency
            }
        };
        const std::array motionWrites{
            GRAPH::RenderGraphResourceUse{
                motionVectors,
                GRAPH::RenderGraphAccess::WriteColor,
                SURFACE::SceneSurfaceProducer::TemporalMotion
            },
            GRAPH::RenderGraphResourceUse{
                motionMetadata,
                GRAPH::RenderGraphAccess::WriteColor,
                SURFACE::SceneSurfaceProducer::TemporalMotion
            },
            GRAPH::RenderGraphResourceUse{
                motionComplete,
                GRAPH::RenderGraphAccess::WriteDependency
            }
        };
        if (!graph.AddPass(GRAPH::RenderGraphPassDesc{
                "MotionVectors",
                motionReads,
                motionWrites,
                callbacks.motionVectors,
                userData
            })) {
            return false;
        }

        GRAPH::RenderGraphResourceId sceneColorOpaque{};
        if (options.captureSceneColor) {
            sceneColorOpaque = AddSurfaceResource(
                graph,
                "SceneColorOpaque",
                GRAPH::RenderGraphSemantic::SceneColorOpaque,
                false,
                GRAPH::RenderGraphSemanticPhase::Opaque,
                SURFACE::SceneSurfaceProducer::SceneColorCapture);
            if (!sceneColorOpaque.IsValid()) {
                return false;
            }
            const std::array captureReads{
                GRAPH::RenderGraphResourceUse{
                    motionComplete,
                    GRAPH::RenderGraphAccess::ReadDependency
                }
            };
            const std::array captureWrites{
                GRAPH::RenderGraphResourceUse{
                    sceneColorOpaque,
                    GRAPH::RenderGraphAccess::CopyDestination,
                    SURFACE::SceneSurfaceProducer::SceneColorCapture
                }
            };
            if (!graph.AddPass(GRAPH::RenderGraphPassDesc{
                    "SceneColorOpaque.Capture",
                    captureReads,
                    captureWrites,
                    callbacks.sceneColorOpaqueCapture,
                    userData
                })) {
                return false;
            }
        }

        if (options.includeSsr) {
            if (!sceneColorOpaque.IsValid()) {
                return false;
            }
            const std::array ssrReads{
                GRAPH::RenderGraphResourceUse{
                    sceneColorOpaque,
                    GRAPH::RenderGraphAccess::ReadSampled
                },
                GRAPH::RenderGraphResourceUse{
                    sceneDepth,
                    GRAPH::RenderGraphAccess::ReadDepth
                },
                GRAPH::RenderGraphResourceUse{
                    packedSurface,
                    GRAPH::RenderGraphAccess::ReadSampled
                },
                GRAPH::RenderGraphResourceUse{
                    currentDepthPyramid,
                    GRAPH::RenderGraphAccess::ReadSampled
                }
            };
            if (!graph.AddPass(GRAPH::RenderGraphPassDesc{
                    "ScreenSpaceReflections",
                    ssrReads,
                    {},
                    callbacks.ssr,
                    userData
                })) {
                return false;
            }
        }
        return true;
    }

} // namespace HIKARI::RENDER3D::PIPELINE::INTERNAL
