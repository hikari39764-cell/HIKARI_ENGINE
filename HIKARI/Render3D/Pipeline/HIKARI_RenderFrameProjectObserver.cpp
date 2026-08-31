#include "Render3D/Pipeline/Internal/HIKARI_RenderFrameGraphStageInternal.h"

#include <array>
#include <cstring>

#include "HIKARI/Project/RenderFrame.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/Surface/HIKARI_SceneSurfaceSet.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"

namespace HIKARI::RENDER3D::PIPELINE::INTERNAL {
namespace {

    GameProjectRenderFrameToken ToProjectFrameToken(
        RenderFrameToken frame) noexcept {
        return { frame.frameIndex, frame.viewId.value, frame.generation };
    }

    GameProjectRenderSurfaceSemantic ToProjectSemantic(
        GRAPH::RenderGraphSemantic value) noexcept {
        using Source = GRAPH::RenderGraphSemantic;
        using Target = GameProjectRenderSurfaceSemantic;
        switch (value) {
        case Source::SceneDepth: return Target::SceneDepth;
        case Source::SceneColorOpaque: return Target::SceneColorOpaque;
        case Source::SceneColorPostOpaque: return Target::SceneColorPostOpaque;
        case Source::SceneColorFinal: return Target::SceneColorFinal;
        case Source::PackedSurface: return Target::PackedSurface;
        case Source::SurfaceFeatureMask: return Target::SurfaceFeatureMask;
        case Source::MotionVectors: return Target::MotionVectors;
        case Source::MotionMetadata: return Target::MotionMetadata;
        case Source::CurrentDepthPyramid: return Target::CurrentDepthPyramid;
        case Source::VisibilityDepthPyramid:
            return Target::VisibilityDepthPyramid;
        case Source::HistoryColor: return Target::HistoryColor;
        case Source::HistoryDepth: return Target::HistoryDepth;
        case Source::StylizedOutlineStyle: return Target::StylizedOutlineStyle;
        case Source::AmbientOcclusion: return Target::AmbientOcclusion;
        case Source::ReactiveMask: return Target::ReactiveMask;
        case Source::TransparencyMask: return Target::TransparencyMask;
        case Source::InvalidDepthMotionMask:
            return Target::InvalidDepthMotionMask;
        case Source::None:
        case Source::Count:
            return Target::None;
        }
        return Target::None;
    }

    GameProjectRenderSurfacePhase ToProjectPhase(
        GRAPH::RenderGraphSemanticPhase value) noexcept {
        using Source = GRAPH::RenderGraphSemanticPhase;
        using Target = GameProjectRenderSurfacePhase;
        switch (value) {
        case Source::Imported: return Target::Imported;
        case Source::PreOpaque: return Target::PreOpaque;
        case Source::Opaque: return Target::Opaque;
        case Source::PostOpaque: return Target::PostOpaque;
        case Source::Temporal: return Target::Temporal;
        case Source::Final: return Target::Final;
        case Source::HistoryRead: return Target::HistoryRead;
        case Source::HistoryWrite: return Target::HistoryWrite;
        case Source::None:
        case Source::Count:
            return Target::None;
        }
        return Target::None;
    }

    GameProjectRenderSurfaceKind ToProjectKind(
        SURFACE::SceneSurfaceResourceKind value) noexcept {
        switch (value) {
        case SURFACE::SceneSurfaceResourceKind::Texture2D:
            return GameProjectRenderSurfaceKind::Texture2D;
        case SURFACE::SceneSurfaceResourceKind::DepthPyramid:
            return GameProjectRenderSurfaceKind::DepthPyramid;
        }
        return GameProjectRenderSurfaceKind::Texture2D;
    }

    GameProjectRenderSurfaceFormat ToProjectFormat(
        SURFACE::SceneSurfaceFormat value) noexcept {
        using Source = SURFACE::SceneSurfaceFormat;
        using Target = GameProjectRenderSurfaceFormat;
        switch (value) {
        case Source::R8Unorm: return Target::R8Unorm;
        case Source::R8G8B8A8Unorm: return Target::R8G8B8A8Unorm;
        case Source::R16G16Float: return Target::R16G16Float;
        case Source::R16G16B16A16Float: return Target::R16G16B16A16Float;
        case Source::R32Float: return Target::R32Float;
        case Source::R32G32Uint: return Target::R32G32Uint;
        case Source::Unknown: return Target::Unknown;
        }
        return Target::Unknown;
    }

    GameProjectRenderSurfaceAvailability ToProjectAvailability(
        SURFACE::SceneSurfaceAvailability value) noexcept {
        using Source = SURFACE::SceneSurfaceAvailability;
        using Target = GameProjectRenderSurfaceAvailability;
        switch (value) {
        case Source::Unavailable: return Target::Unavailable;
        case Source::Imported: return Target::Imported;
        case Source::Produced: return Target::Produced;
        case Source::Alias: return Target::Alias;
        }
        return Target::Unavailable;
    }

    GameProjectRenderSurfaceLifetime ToProjectLifetime(
        SURFACE::SceneSurfaceLifetime value) noexcept {
        using Source = SURFACE::SceneSurfaceLifetime;
        using Target = GameProjectRenderSurfaceLifetime;
        switch (value) {
        case Source::FrameLocal: return Target::FrameLocal;
        case Source::History: return Target::History;
        case Source::FrameOrHistory: return Target::FrameOrHistory;
        }
        return Target::FrameLocal;
    }

    GameProjectRenderSurfaceFallback ToProjectFallback(
        SURFACE::SceneSurfaceFallbackPolicy value) noexcept {
        using Source = SURFACE::SceneSurfaceFallbackPolicy;
        using Target = GameProjectRenderSurfaceFallback;
        switch (value) {
        case Source::RequiredFail: return Target::RequiredFail;
        case Source::SkipProvider: return Target::SkipProvider;
        case Source::NeutralBlack: return Target::NeutralBlack;
        case Source::NeutralWhite: return Target::NeutralWhite;
        case Source::NoHistory: return Target::NoHistory;
        }
        return Target::SkipProvider;
    }

    GameProjectRenderViewPurpose ToProjectPurpose(
        RenderViewPurpose value) noexcept {
        using Target = GameProjectRenderViewPurpose;
        switch (value) {
        case RenderViewPurpose::Game: return Target::Game;
        case RenderViewPurpose::EditorScene: return Target::EditorScene;
        case RenderViewPurpose::ToolPreview: return Target::ToolPreview;
        case RenderViewPurpose::ReflectionProbeCapture:
            return Target::ReflectionProbeCapture;
        case RenderViewPurpose::LightProbeCapture:
            return Target::LightProbeCapture;
        }
        return Target::Game;
    }

    uint32_t ToProjectReadAccessMask(uint32_t value) noexcept {
        uint32_t result = 0u;
        const auto append = [&result, value](
            SURFACE::SceneSurfaceAccess source,
            GameProjectRenderSurfaceAccess target) noexcept {
            if ((value & static_cast<uint32_t>(source)) != 0u) {
                result |= static_cast<uint32_t>(target);
            }
        };
        append(SURFACE::SceneSurfaceAccess::Sampled,
            GameProjectRenderSurfaceAccess::Sampled);
        append(SURFACE::SceneSurfaceAccess::ReadDepth,
            GameProjectRenderSurfaceAccess::ReadDepth);
        append(SURFACE::SceneSurfaceAccess::ReadUav,
            GameProjectRenderSurfaceAccess::ReadUav);
        append(SURFACE::SceneSurfaceAccess::CopySource,
            GameProjectRenderSurfaceAccess::CopySource);
        return result;
    }

    void CopyMatrix(
        float (&destination)[16],
        const MATH::Mat4& source) noexcept {
        std::memcpy(destination, source.m, sizeof(source.m));
    }

    GameProjectRenderCameraFrame BuildProjectCameraFrame(
        const RenderFrameGraphStageContext& context) noexcept {
        const TEMPORAL::TemporalCameraData& source =
            context.temporalFrame.camera;
        GameProjectRenderCameraFrame camera{};
        CopyMatrix(camera.view, source.view);
        CopyMatrix(camera.projection, source.proj);
        CopyMatrix(camera.viewProjection, source.viewProj);
        CopyMatrix(camera.inverseViewProjection, source.invViewProj);
        CopyMatrix(
            camera.unjitteredViewProjection,
            source.unjitteredViewProj);
        CopyMatrix(camera.previousViewProjection, source.prevViewProj);
        camera.position[0] = source.cameraPos.x;
        camera.position[1] = source.cameraPos.y;
        camera.position[2] = source.cameraPos.z;
        camera.previousPosition[0] = source.prevCameraPos.x;
        camera.previousPosition[1] = source.prevCameraPos.y;
        camera.previousPosition[2] = source.prevCameraPos.z;
        camera.jitterPixels[0] = source.jitter.x;
        camera.jitterPixels[1] = source.jitter.y;
        camera.previousJitterPixels[0] = source.prevJitter.x;
        camera.previousJitterPixels[1] = source.prevJitter.y;
        camera.nearZ = source.nearZ;
        camera.farZ = source.farZ;
        camera.fovYRadians = source.fovYRad;
        camera.aspect = source.aspect;
        camera.renderWidth = context.temporalFrame.renderWidth;
        camera.renderHeight = context.temporalFrame.renderHeight;
        camera.outputWidth = context.temporalFrame.outputWidth;
        camera.outputHeight = context.temporalFrame.outputHeight;
        camera.sourceCameraObjectId = context.frame.cameraSourceObjectId;
        camera.cameraRevision = context.frame.cameraRevision;
        if (context.frame.cameraCut) {
            camera.flags |= static_cast<uint32_t>(
                GameProjectRenderFrameFlags::CameraCut);
        }
        if (context.frame.projectionChanged) {
            camera.flags |= static_cast<uint32_t>(
                GameProjectRenderFrameFlags::ProjectionChanged);
        }
        if (source.previousValid) {
            camera.flags |= static_cast<uint32_t>(
                GameProjectRenderFrameFlags::PreviousCameraValid);
        }
        if (source.jittered) {
            camera.flags |= static_cast<uint32_t>(
                GameProjectRenderFrameFlags::Jittered);
        }
        if (source.valid) {
            camera.flags |= static_cast<uint32_t>(
                GameProjectRenderFrameFlags::CameraValid);
        }
        return camera;
    }

    bool AppendProjectSurface(
        const SURFACE::SceneSurfaceDescriptor& source,
        std::array<GameProjectRenderSurfaceDesc,
            kGameProjectRenderMaximumSurfaceCount>& destination,
        uint32_t& count) noexcept {
        if (count >= destination.size()) {
            return false;
        }
        GameProjectRenderSurfaceDesc& surface = destination[count++];
        surface.handle.frame = ToProjectFrameToken(
            source.publication.lease.frame);
        surface.handle.publicationGeneration =
            source.publication.lease.publicationGeneration;
        surface.handle.storageGeneration =
            source.publication.lease.storageGeneration;
        surface.handle.semantic =
            ToProjectSemantic(source.publication.key.semantic);
        surface.handle.phase = ToProjectPhase(source.publication.key.phase);
        surface.kind = ToProjectKind(source.semantic.resourceKind);
        surface.availability =
            ToProjectAvailability(source.publication.availability);
        surface.format = ToProjectFormat(source.semantic.format);
        surface.lifetime = ToProjectLifetime(source.semantic.lifetime);
        surface.fallback = ToProjectFallback(source.semantic.fallback);
        surface.sampleable = source.sampleable ? 1u : 0u;
        surface.width = source.width;
        surface.height = source.height;
        surface.mipCount = source.mipCount;
        surface.readAccessMask =
            ToProjectReadAccessMask(source.semantic.allowedAccessMask);
        return surface.handle.IsValid();
    }

} // namespace

bool ExecuteProjectRenderObserver(
    const GRAPH::RenderGraphPassContext& graphPass,
    void* userData) noexcept {
    auto& state = *static_cast<StageExecutionState*>(userData);
    if (!ValidateRenderFrameGraphPassContext(graphPass, state)) {
        return false;
    }
    const RenderFrameGraphStageContext& context = *state.context;
    if (!context.frame.projectRenderObserver.IsValid()) {
        return true;
    }

    std::array<GameProjectRenderSurfaceDesc,
        kGameProjectRenderMaximumSurfaceCount> surfaces{};
    uint32_t surfaceCount = 0u;
    bool surfaceListTruncated = false;
    using Semantic = GRAPH::RenderGraphSemantic;
    using Phase = GRAPH::RenderGraphSemanticPhase;
    for (uint16_t semanticValue = 1u;
        semanticValue < static_cast<uint16_t>(Semantic::Count);
        ++semanticValue) {
        for (uint8_t phaseValue = 1u;
            phaseValue < static_cast<uint8_t>(Phase::Count);
            ++phaseValue) {
            SURFACE::SceneSurfaceDescriptor source{};
            if (!context.sceneSurfaces.TryDescribe(
                    GRAPH::RenderGraphSurfaceKey{
                        static_cast<Semantic>(semanticValue),
                        static_cast<Phase>(phaseValue)
                    },
                    source)) {
                continue;
            }
            if (!AppendProjectSurface(source, surfaces, surfaceCount)) {
                surfaceListTruncated = true;
                break;
            }
        }
        if (surfaceListTruncated) {
            break;
        }
    }

    const GameProjectRenderCameraFrame camera =
        BuildProjectCameraFrame(context);
    GameProjectRenderFrameView frame{};
    frame.frame = ToProjectFrameToken(context.frame.token);
    frame.stage = GameProjectRenderObserverStage::PostOpaque;
    frame.purpose = ToProjectPurpose(context.frame.purpose);
    frame.flags = camera.flags;
    if (surfaceListTruncated) {
        frame.flags |= static_cast<uint32_t>(
            GameProjectRenderFrameFlags::SurfaceListTruncated);
    }
    const uint32_t surfaceCapability = static_cast<uint32_t>(
        GameProjectRenderCapability::SurfaceMetadata);
    const uint32_t cameraCapability = static_cast<uint32_t>(
        GameProjectRenderCapability::Camera);
    const uint32_t exposureCapability = static_cast<uint32_t>(
        GameProjectRenderCapability::Exposure);
    frame.supportedCapabilityMask =
        surfaceCapability | cameraCapability | exposureCapability;
    frame.availableCapabilityMask = cameraCapability |
        (surfaceCount != 0u ? surfaceCapability : 0u);
    frame.validCapabilityMask =
        surfaceCount != 0u && !surfaceListTruncated
        ? surfaceCapability
        : 0u;
    if ((camera.flags & static_cast<uint32_t>(
            GameProjectRenderFrameFlags::CameraValid)) != 0u) {
        frame.validCapabilityMask |= cameraCapability;
    }
    frame.surfaceCount = surfaceCount;
    frame.surfaces = surfaces.data();
    frame.camera = &camera;
    frame.viewGeneration = context.temporalFrame.view.generation;
    frame.worldIdentity =
        context.frame.projectRenderObserver.worldIdentity;

    const TEMPORAL::TemporalResourceStats temporal =
        TEMPORAL::GetLatestTemporalResourceStats(context.frame.viewId);
    if (temporal.resourceFrame.Matches(context.temporalResources) &&
        temporal.exposureWritten) {
        frame.exposureMultiplier = temporal.exposureMultiplier;
        frame.preExposure = temporal.preExposure;
        frame.exposureMode = temporal.exposureMode ==
            COLOR::ExposureMode::Automatic
            ? GameProjectRenderExposureMode::Automatic
            : GameProjectRenderExposureMode::Manual;
        frame.exposureValid = 1u;
        frame.availableCapabilityMask |= exposureCapability;
        frame.validCapabilityMask |= exposureCapability;
    }
    (void)context.frame.projectRenderObserver.observe(
        context.frame.projectRenderObserver.context,
        frame);
    return true;
}

} // namespace HIKARI::RENDER3D::PIPELINE::INTERNAL
