#include "Render3D/Pipeline/Internal/HIKARI_RenderFrameGraphStageInternal.h"

#include <array>

namespace HIKARI::RENDER3D::PIPELINE::INTERNAL {
    namespace {
        GRAPH::RenderGraphResourceId AddResource(
            GRAPH::RenderGraph& graph,
            std::string_view id,
            GRAPH::RenderGraphSemantic semantic,
            bool imported) noexcept {
            return graph.AddResource(GRAPH::RenderGraphResourceDesc{
                id,
                semantic,
                imported
            });
        }
    }

    bool BuildRenderFrameGraphPlan(
        const RenderFrameGraphPlanOptions& options,
        void* userData,
        const RenderFrameGraphPassCallbacks& callbacks,
        GRAPH::RenderGraph& graph) noexcept {
        const auto sceneDepth = AddResource(
            graph,
            "SceneDepth",
            GRAPH::RenderGraphSemantic::SceneDepth,
            true);
        const auto packedSurfaceInput = AddResource(
            graph,
            "PackedSurface.PreOpaque",
            GRAPH::RenderGraphSemantic::None,
            true);
        const auto packedSurface = AddResource(
            graph,
            "PackedSurface",
            GRAPH::RenderGraphSemantic::PackedSurface,
            false);
        const auto currentDepthPyramid = AddResource(
            graph,
            "CurrentDepthPyramid",
            GRAPH::RenderGraphSemantic::CurrentDepthPyramid,
            false);
        const auto forwardDepthPyramid = AddResource(
            graph,
            "DepthPyramid.Forward",
            GRAPH::RenderGraphSemantic::None,
            true);
        const auto ambientOcclusion = AddResource(
            graph,
            "AmbientOcclusion.Forward",
            GRAPH::RenderGraphSemantic::None,
            true);
        const auto surfaceFeatureMask = AddResource(
            graph,
            "SurfaceFeatureMask",
            GRAPH::RenderGraphSemantic::SurfaceFeatureMask,
            false);
        const auto forwardComplete = AddResource(
            graph,
            "ForwardOpaque.Complete",
            GRAPH::RenderGraphSemantic::None,
            false);
        const auto postOpaqueComplete = AddResource(
            graph,
            "PostOpaqueSurfaces.Complete",
            GRAPH::RenderGraphSemantic::None,
            false);
        const auto cloudsComplete = AddResource(
            graph,
            "Clouds.Complete",
            GRAPH::RenderGraphSemantic::None,
            false);
        const auto motionVectors = AddResource(
            graph,
            "MotionVectors",
            GRAPH::RenderGraphSemantic::MotionVectors,
            false);
        const auto motionMetadata = AddResource(
            graph,
            "MotionMetadata",
            GRAPH::RenderGraphSemantic::MotionMetadata,
            false);
        if (!sceneDepth.IsValid() ||
            !packedSurfaceInput.IsValid() ||
            !packedSurface.IsValid() ||
            !currentDepthPyramid.IsValid() ||
            !forwardDepthPyramid.IsValid() ||
            !ambientOcclusion.IsValid() ||
            !surfaceFeatureMask.IsValid() ||
            !forwardComplete.IsValid() ||
            !postOpaqueComplete.IsValid() ||
            !cloudsComplete.IsValid() ||
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
                GRAPH::RenderGraphAccess::WriteColor
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
                GRAPH::RenderGraphAccess::ReadSampled
            },
            GRAPH::RenderGraphResourceUse{
                sceneDepth,
                GRAPH::RenderGraphAccess::ReadDepth
            }
        };
        const std::array postWrites{
            GRAPH::RenderGraphResourceUse{
                postOpaqueComplete,
                GRAPH::RenderGraphAccess::WriteColor
            },
            GRAPH::RenderGraphResourceUse{
                packedSurface,
                GRAPH::RenderGraphAccess::WriteColor
            },
            GRAPH::RenderGraphResourceUse{
                currentDepthPyramid,
                GRAPH::RenderGraphAccess::WriteUav
            },
            GRAPH::RenderGraphResourceUse{
                surfaceFeatureMask,
                GRAPH::RenderGraphAccess::WriteColor
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
                GRAPH::RenderGraphAccess::ReadSampled
            },
            GRAPH::RenderGraphResourceUse{
                sceneDepth,
                GRAPH::RenderGraphAccess::ReadDepth
            }
        };
        const std::array cloudWrites{
            GRAPH::RenderGraphResourceUse{
                cloudsComplete,
                GRAPH::RenderGraphAccess::WriteColor
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
                GRAPH::RenderGraphAccess::ReadSampled
            }
        };
        const std::array motionWrites{
            GRAPH::RenderGraphResourceUse{
                motionVectors,
                GRAPH::RenderGraphAccess::WriteColor
            },
            GRAPH::RenderGraphResourceUse{
                motionMetadata,
                GRAPH::RenderGraphAccess::WriteColor
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
            sceneColorOpaque = AddResource(
                graph,
                "SceneColorOpaque",
                GRAPH::RenderGraphSemantic::SceneColorOpaque,
                false);
            if (!sceneColorOpaque.IsValid()) {
                return false;
            }
            const std::array captureReads{
                GRAPH::RenderGraphResourceUse{
                    motionVectors,
                    GRAPH::RenderGraphAccess::ReadSampled
                }
            };
            const std::array captureWrites{
                GRAPH::RenderGraphResourceUse{
                    sceneColorOpaque,
                    GRAPH::RenderGraphAccess::CopyDestination
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
            const auto ssrComplete = AddResource(
                graph,
                "SSR.Complete",
                GRAPH::RenderGraphSemantic::None,
                false);
            if (!ssrComplete.IsValid()) {
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
            const std::array ssrWrites{
                GRAPH::RenderGraphResourceUse{
                    ssrComplete,
                    GRAPH::RenderGraphAccess::WriteColor
                }
            };
            if (!graph.AddPass(GRAPH::RenderGraphPassDesc{
                    "ScreenSpaceReflections",
                    ssrReads,
                    ssrWrites,
                    callbacks.ssr,
                    userData
                })) {
                return false;
            }
        }
        return true;
    }

} // namespace HIKARI::RENDER3D::PIPELINE::INTERNAL
