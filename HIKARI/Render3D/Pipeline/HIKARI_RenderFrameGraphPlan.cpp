#include "Render3D/Pipeline/Internal/HIKARI_RenderFrameGraphStageInternal.h"

#include <array>
#include <cstddef>
#include <span>

#include "Render3D/Graph/HIKARI_RenderGraphSurfaceContract.h"

namespace HIKARI::RENDER3D::PIPELINE::INTERNAL {
GRAPH::RenderGraphResourceId AddRenderFrameGraphSurfaceResource(
    GRAPH::RenderGraph& graph, std::string_view id, GRAPH::RenderGraphSemantic semantic, bool imported,
    GRAPH::RenderGraphSemanticPhase phase, SURFACE::SceneSurfaceProducer producer,
    GRAPH::RenderGraphResourceRequirement requirement) noexcept {
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

GRAPH::RenderGraphResourceId AddRenderFrameGraphCompletionMarker(GRAPH::RenderGraph& graph,
                                                                 std::string_view id) noexcept {
    GRAPH::RenderGraphResourceDesc desc{};
    desc.id = id;
    desc.kind = GRAPH::RenderGraphResourceKind::CompletionMarker;
    return graph.AddResource(desc);
}

bool BuildRenderFrameGraphPlan(const RenderFrameGraphPlanOptions& options, void* userData,
                               const RenderFrameGraphPassCallbacks& callbacks, GRAPH::RenderGraph& graph) noexcept {
    const bool captureSceneColor =
        options.captureSceneColor ||
        (options.projectExtensions != nullptr &&
         ProjectRenderPassesRequireSceneColorOpaque(*options.projectExtensions, options.projectPurpose));
    const bool includeProjectDispatchOutput =
        options.projectExtensions != nullptr &&
        ProjectRenderPassesRequireDispatchOutput(*options.projectExtensions, options.projectPurpose);
    const auto sceneDepth = AddRenderFrameGraphSurfaceResource(
        graph, "SceneDepth", GRAPH::RenderGraphSemantic::SceneDepth, true, GRAPH::RenderGraphSemanticPhase::Imported,
        SURFACE::SceneSurfaceProducer::PostSceneDepth);
    const auto packedSurfaceInput = AddRenderFrameGraphSurfaceResource(
        graph, "PackedSurface.PreOpaque", GRAPH::RenderGraphSemantic::PackedSurface, true,
        GRAPH::RenderGraphSemanticPhase::PreOpaque, SURFACE::SceneSurfaceProducer::SceneSurfaceAux,
        GRAPH::RenderGraphResourceRequirement::Optional);
    const auto packedSurface = AddRenderFrameGraphSurfaceResource(
        graph, "PackedSurface", GRAPH::RenderGraphSemantic::PackedSurface, false,
        GRAPH::RenderGraphSemanticPhase::PostOpaque, SURFACE::SceneSurfaceProducer::SceneSurfaceAux,
        GRAPH::RenderGraphResourceRequirement::Optional);
    const auto currentDepthPyramid = AddRenderFrameGraphSurfaceResource(
        graph, "CurrentDepthPyramid", GRAPH::RenderGraphSemantic::CurrentDepthPyramid, false,
        GRAPH::RenderGraphSemanticPhase::PostOpaque, SURFACE::SceneSurfaceProducer::DepthPyramid,
        GRAPH::RenderGraphResourceRequirement::Optional);
    const auto forwardDepthPyramid = AddRenderFrameGraphSurfaceResource(
        graph, "DepthPyramid.Forward", GRAPH::RenderGraphSemantic::VisibilityDepthPyramid, true,
        GRAPH::RenderGraphSemanticPhase::PreOpaque, SURFACE::SceneSurfaceProducer::DepthPyramid,
        GRAPH::RenderGraphResourceRequirement::Optional);
    const auto ambientOcclusionInput = AddRenderFrameGraphSurfaceResource(
        graph, "AmbientOcclusion.Forward", GRAPH::RenderGraphSemantic::AmbientOcclusion, true,
        GRAPH::RenderGraphSemanticPhase::PreOpaque, SURFACE::SceneSurfaceProducer::Ssao,
        GRAPH::RenderGraphResourceRequirement::Optional);
    const auto ambientOcclusion = AddRenderFrameGraphSurfaceResource(
        graph, "AmbientOcclusion", GRAPH::RenderGraphSemantic::AmbientOcclusion, false,
        GRAPH::RenderGraphSemanticPhase::PostOpaque, SURFACE::SceneSurfaceProducer::Ssao,
        GRAPH::RenderGraphResourceRequirement::Optional);
    const auto visibilityDepthPyramid = AddRenderFrameGraphSurfaceResource(
        graph, "VisibilityDepthPyramid", GRAPH::RenderGraphSemantic::VisibilityDepthPyramid, false,
        GRAPH::RenderGraphSemanticPhase::PostOpaque, SURFACE::SceneSurfaceProducer::DepthPyramid,
        GRAPH::RenderGraphResourceRequirement::Optional);
    const auto surfaceFeatureMask = AddRenderFrameGraphSurfaceResource(
        graph, "SurfaceFeatureMask", GRAPH::RenderGraphSemantic::SurfaceFeatureMask, false,
        GRAPH::RenderGraphSemanticPhase::PostOpaque, SURFACE::SceneSurfaceProducer::SceneSurfaceAux,
        GRAPH::RenderGraphResourceRequirement::Optional);
    const auto stylizedOutlineStyle = AddRenderFrameGraphSurfaceResource(
        graph, "StylizedOutlineStyle", GRAPH::RenderGraphSemantic::StylizedOutlineStyle, false,
        GRAPH::RenderGraphSemanticPhase::PostOpaque, SURFACE::SceneSurfaceProducer::SceneSurfaceAux,
        GRAPH::RenderGraphResourceRequirement::Optional);
    GRAPH::RenderGraphResourceId projectDispatchOutput{};
    if (includeProjectDispatchOutput) {
        projectDispatchOutput = AddRenderFrameGraphSurfaceResource(
            graph, "ProjectDispatchOutput", GRAPH::RenderGraphSemantic::ProjectDispatchOutput, false,
            GRAPH::RenderGraphSemanticPhase::PostOpaque, SURFACE::SceneSurfaceProducer::ProjectDispatch,
            GRAPH::RenderGraphResourceRequirement::Optional);
    }
    GRAPH::RenderGraphResourceId reflectionFallback{}, reflectionResponse{}, reflectionMaterial{};
    if (options.includeSsr) {
        reflectionFallback = AddRenderFrameGraphSurfaceResource(
            graph, "ReflectionFallbackSpecular", GRAPH::RenderGraphSemantic::ReflectionFallbackSpecular, false,
            GRAPH::RenderGraphSemanticPhase::Opaque, SURFACE::SceneSurfaceProducer::ForwardOpaque,
            GRAPH::RenderGraphResourceRequirement::Optional);
        reflectionResponse = AddRenderFrameGraphSurfaceResource(
            graph, "ReflectionSpecularResponse", GRAPH::RenderGraphSemantic::ReflectionSpecularResponse, false,
            GRAPH::RenderGraphSemanticPhase::Opaque, SURFACE::SceneSurfaceProducer::ForwardOpaque,
            GRAPH::RenderGraphResourceRequirement::Optional);
        reflectionMaterial = AddRenderFrameGraphSurfaceResource(
            graph, "ReflectionMaterial", GRAPH::RenderGraphSemantic::ReflectionMaterial, false,
            GRAPH::RenderGraphSemanticPhase::Opaque, SURFACE::SceneSurfaceProducer::ForwardOpaque,
            GRAPH::RenderGraphResourceRequirement::Optional);
        if (!reflectionFallback.IsValid() || !reflectionResponse.IsValid() || !reflectionMaterial.IsValid())
            return false;
    }
    const auto forwardComplete = AddRenderFrameGraphCompletionMarker(graph, "ForwardOpaque.Complete");
    const auto postOpaqueComplete = AddRenderFrameGraphCompletionMarker(graph, "PostOpaqueSurfaces.Complete");
    const auto cloudsComplete = AddRenderFrameGraphCompletionMarker(graph, "Clouds.Complete");
    const auto motionComplete = AddRenderFrameGraphCompletionMarker(graph, "MotionVectors.Complete");
    const auto preTailComplete = AddRenderFrameGraphCompletionMarker(graph, "OpaqueLighting.Complete");
    const auto motionVectors = AddRenderFrameGraphSurfaceResource(
        graph, "MotionVectors", GRAPH::RenderGraphSemantic::MotionVectors, false,
        GRAPH::RenderGraphSemanticPhase::Temporal, SURFACE::SceneSurfaceProducer::TemporalMotion);
    const auto motionMetadata = AddRenderFrameGraphSurfaceResource(
        graph, "MotionMetadata", GRAPH::RenderGraphSemantic::MotionMetadata, false,
        GRAPH::RenderGraphSemanticPhase::Temporal, SURFACE::SceneSurfaceProducer::TemporalMotion);
    if (!sceneDepth.IsValid() || !packedSurfaceInput.IsValid() || !packedSurface.IsValid() ||
        !currentDepthPyramid.IsValid() || !forwardDepthPyramid.IsValid() || !ambientOcclusionInput.IsValid() ||
        !ambientOcclusion.IsValid() || !visibilityDepthPyramid.IsValid() || !surfaceFeatureMask.IsValid() ||
        !stylizedOutlineStyle.IsValid() || (includeProjectDispatchOutput && !projectDispatchOutput.IsValid()) ||
        !forwardComplete.IsValid() || !postOpaqueComplete.IsValid() || !cloudsComplete.IsValid() ||
        !motionComplete.IsValid() || !preTailComplete.IsValid() || !motionVectors.IsValid() ||
        !motionMetadata.IsValid()) {
        return false;
    }

    const std::array forwardReads{
        GRAPH::RenderGraphResourceUse{sceneDepth, GRAPH::RenderGraphAccess::ReadDepth},
        GRAPH::RenderGraphResourceUse{packedSurfaceInput, GRAPH::RenderGraphAccess::ReadSampled},
        GRAPH::RenderGraphResourceUse{forwardDepthPyramid, GRAPH::RenderGraphAccess::ReadSampled},
        GRAPH::RenderGraphResourceUse{ambientOcclusionInput, GRAPH::RenderGraphAccess::ReadSampled}};
    const std::array forwardWrites{
        GRAPH::RenderGraphResourceUse{forwardComplete, GRAPH::RenderGraphAccess::WriteDependency},
        GRAPH::RenderGraphResourceUse{reflectionFallback, GRAPH::RenderGraphAccess::WriteColor,
                                      SURFACE::SceneSurfaceProducer::ForwardOpaque},
        GRAPH::RenderGraphResourceUse{reflectionResponse, GRAPH::RenderGraphAccess::WriteColor,
                                      SURFACE::SceneSurfaceProducer::ForwardOpaque},
        GRAPH::RenderGraphResourceUse{reflectionMaterial, GRAPH::RenderGraphAccess::WriteColor,
                                      SURFACE::SceneSurfaceProducer::ForwardOpaque}};
    if (!graph.AddPass(GRAPH::RenderGraphPassDesc{"ForwardOpaque", forwardReads,
                                                  std::span(forwardWrites.data(), options.includeSsr ? 4u : 1u),
                                                  callbacks.forwardOpaque, userData})) {
        return false;
    }

    const std::array postReads{GRAPH::RenderGraphResourceUse{forwardComplete, GRAPH::RenderGraphAccess::ReadDependency},
                               GRAPH::RenderGraphResourceUse{sceneDepth, GRAPH::RenderGraphAccess::ReadDepth}};
    const std::array postWrites{
        GRAPH::RenderGraphResourceUse{postOpaqueComplete, GRAPH::RenderGraphAccess::WriteDependency},
        GRAPH::RenderGraphResourceUse{packedSurface, GRAPH::RenderGraphAccess::WriteColor,
                                      SURFACE::SceneSurfaceProducer::SceneSurfaceAux},
        GRAPH::RenderGraphResourceUse{currentDepthPyramid, GRAPH::RenderGraphAccess::WriteUav,
                                      SURFACE::SceneSurfaceProducer::DepthPyramid},
        GRAPH::RenderGraphResourceUse{visibilityDepthPyramid, GRAPH::RenderGraphAccess::WriteUav,
                                      SURFACE::SceneSurfaceProducer::DepthPyramid},
        GRAPH::RenderGraphResourceUse{ambientOcclusion, GRAPH::RenderGraphAccess::WriteColor,
                                      SURFACE::SceneSurfaceProducer::Ssao},
        GRAPH::RenderGraphResourceUse{surfaceFeatureMask, GRAPH::RenderGraphAccess::WriteColor,
                                      SURFACE::SceneSurfaceProducer::SceneSurfaceAux},
        GRAPH::RenderGraphResourceUse{stylizedOutlineStyle, GRAPH::RenderGraphAccess::WriteColor,
                                      SURFACE::SceneSurfaceProducer::SceneSurfaceAux}};
    if (!graph.AddPass(GRAPH::RenderGraphPassDesc{"ScreenSpacePostOpaque", postReads, postWrites,
                                                  callbacks.postOpaqueSurfaces, userData})) {
        return false;
    }

    const std::array motionReads{
        GRAPH::RenderGraphResourceUse{sceneDepth, GRAPH::RenderGraphAccess::ReadDepth},
        GRAPH::RenderGraphResourceUse{postOpaqueComplete, GRAPH::RenderGraphAccess::ReadDependency}};
    std::array<GRAPH::RenderGraphResourceUse, 4u> motionWrites{
        GRAPH::RenderGraphResourceUse{motionVectors, GRAPH::RenderGraphAccess::WriteColor,
                                      SURFACE::SceneSurfaceProducer::TemporalMotion},
        GRAPH::RenderGraphResourceUse{motionMetadata, GRAPH::RenderGraphAccess::WriteColor,
                                      SURFACE::SceneSurfaceProducer::TemporalMotion},
        GRAPH::RenderGraphResourceUse{motionComplete, GRAPH::RenderGraphAccess::WriteDependency},
        GRAPH::RenderGraphResourceUse{}};
    std::size_t motionWriteCount = 3u;
    if (!captureSceneColor) {
        motionWrites[motionWriteCount++] =
            GRAPH::RenderGraphResourceUse{preTailComplete, GRAPH::RenderGraphAccess::WriteDependency};
    }
    if (!graph.AddPass(GRAPH::RenderGraphPassDesc{"MotionVectors", motionReads,
                                                  std::span(motionWrites.data(), motionWriteCount),
                                                  callbacks.motionVectors, userData})) {
        return false;
    }

    GRAPH::RenderGraphResourceId sceneColorOpaque{};
    if (captureSceneColor) {
        sceneColorOpaque = AddRenderFrameGraphSurfaceResource(
            graph, "SceneColorOpaque", GRAPH::RenderGraphSemantic::SceneColorOpaque, false,
            GRAPH::RenderGraphSemanticPhase::Opaque, SURFACE::SceneSurfaceProducer::SceneColorCapture);
        if (!sceneColorOpaque.IsValid()) {
            return false;
        }
        const std::array captureReads{
            GRAPH::RenderGraphResourceUse{motionComplete, GRAPH::RenderGraphAccess::ReadDependency}};
        std::array<GRAPH::RenderGraphResourceUse, 2u> captureWrites{
            GRAPH::RenderGraphResourceUse{sceneColorOpaque, GRAPH::RenderGraphAccess::CopyDestination,
                                          SURFACE::SceneSurfaceProducer::SceneColorCapture},
            GRAPH::RenderGraphResourceUse{}};
        std::size_t captureWriteCount = 1u;
        if (!options.includeSsr) {
            captureWrites[captureWriteCount++] =
                GRAPH::RenderGraphResourceUse{preTailComplete, GRAPH::RenderGraphAccess::WriteDependency};
        }
        if (!graph.AddPass(GRAPH::RenderGraphPassDesc{"SceneColorOpaque.Capture", captureReads,
                                                      std::span(captureWrites.data(), captureWriteCount),
                                                      callbacks.sceneColorOpaqueCapture, userData})) {
            return false;
        }
    }

    if (options.includeSsr) {
        if (!sceneColorOpaque.IsValid()) {
            return false;
        }
        const std::array ssrReads{
            GRAPH::RenderGraphResourceUse{sceneColorOpaque, GRAPH::RenderGraphAccess::ReadSampled},
            GRAPH::RenderGraphResourceUse{sceneDepth, GRAPH::RenderGraphAccess::ReadDepth},
            GRAPH::RenderGraphResourceUse{packedSurface, GRAPH::RenderGraphAccess::ReadSampled},
            GRAPH::RenderGraphResourceUse{currentDepthPyramid, GRAPH::RenderGraphAccess::ReadSampled},
            GRAPH::RenderGraphResourceUse{reflectionFallback, GRAPH::RenderGraphAccess::ReadSampled},
            GRAPH::RenderGraphResourceUse{reflectionResponse, GRAPH::RenderGraphAccess::ReadSampled},
            GRAPH::RenderGraphResourceUse{reflectionMaterial, GRAPH::RenderGraphAccess::ReadSampled},
            GRAPH::RenderGraphResourceUse{motionVectors, GRAPH::RenderGraphAccess::ReadSampled},
            GRAPH::RenderGraphResourceUse{motionMetadata, GRAPH::RenderGraphAccess::ReadSampled}};
        const std::array ssrWrites{
            GRAPH::RenderGraphResourceUse{preTailComplete, GRAPH::RenderGraphAccess::WriteDependency}};
        if (!graph.AddPass(
                GRAPH::RenderGraphPassDesc{"ScreenSpaceReflections", ssrReads, ssrWrites, callbacks.ssr, userData})) {
            return false;
        }
    }
    // SSR は不透明面の fallback lobe を置換してから、雲の透過/散乱を合成する。
    // 雲を含む SceneColor から未減衰の fallback を引くと反射のエネルギーが壊れる。
    const std::array cloudReads{
        GRAPH::RenderGraphResourceUse{preTailComplete, GRAPH::RenderGraphAccess::ReadDependency},
        GRAPH::RenderGraphResourceUse{sceneDepth, GRAPH::RenderGraphAccess::ReadDepth}};
    const std::array cloudWrites{
        GRAPH::RenderGraphResourceUse{cloudsComplete, GRAPH::RenderGraphAccess::WriteDependency}};
    if (!graph.AddPass(
            GRAPH::RenderGraphPassDesc{"VolumetricClouds", cloudReads, cloudWrites, callbacks.clouds, userData}))
        return false;
    return BuildRenderFrameGraphTailPlan(
        options, userData, callbacks,
        RenderFrameGraphTailResources{cloudsComplete, sceneDepth, sceneColorOpaque, packedSurface, ambientOcclusion,
                                      currentDepthPyramid, visibilityDepthPyramid, surfaceFeatureMask,
                                      stylizedOutlineStyle, projectDispatchOutput},
        graph);
}

} // namespace HIKARI::RENDER3D::PIPELINE::INTERNAL
