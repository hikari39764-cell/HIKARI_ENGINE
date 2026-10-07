#include "Render3D/Temporal/HIKARI_TemporalResolveStage.h"

#include <algorithm>

#include "Core/HIKARI_TimeService.h"
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/Debug/HIKARI_RenderDebugOutput.h"
#include "Render3D/Debug/HIKARI_RenderDebugView.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Temporal/HIKARI_TaaResolvePass.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"
#include "Render3D/Temporal/HIKARI_TemporalMaskPass.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"
#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"

namespace HIKARI::RENDER3D::TEMPORAL {

namespace {
void RejectTemporalFrame(const TemporalFrameState& frame, TemporalResourceFrame resources, bool nativeTaaRequested) {
    if (resources.IsValid()) {
        (void)MarkTemporalAntiAliasing(resources, nativeTaaRequested, false);
        (void)AbortTemporalResources(resources);
    } else {
        (void)AbortTemporalFrame(frame.view, frame.publication, TemporalHistoryResetReason::ExplicitReset);
    }
}

TaaResolveSettings BuildTaaSettings(const RenderQualitySettings& quality) {
    TaaResolveSettings settings{};
    settings.enabled = true;
    settings.historyWeight = quality.taaHistoryWeight;
    settings.varianceClipGamma = quality.taaVarianceClipGamma;
    settings.depthRejection = quality.taaDepthRejection;
    settings.luminanceRejection = quality.taaLuminanceRejection;
    settings.sharpness = quality.taaSharpness;
    return settings;
}
} // namespace

TemporalResolveStageResult ExecuteTemporalResolveStage(TemporalViewLease view, RenderFrameToken publication,
                                                       TemporalResourceFrame resources, const GFX::Context& graphics,
                                                       RenderTarget2D& sceneColorTarget,
                                                       RenderTarget2D& sceneDepthTarget,
                                                       const RenderQualitySettings& quality,
                                                       const COLOR::ExposureFrameState& exposureState) {
    TemporalResolveStageResult result{};
    result.output = &sceneColorTarget;

    TemporalFrameState frame{};
    if (!TryGetTemporalFrameState(view, publication, frame)) {
        return result;
    }
    result.outputJitterPixels = {frame.camera.jitter.x, frame.camera.jitter.y};
    if (!resources.IsValid() || !resources.view.Matches(view) || !resources.publication.Matches(publication)) {
        (void)AbortTemporalFrame(view, publication, TemporalHistoryResetReason::InvalidInput);
        return result;
    }

    const bool frameCurrent =
        frame.publication.Matches(publication) && frame.frameIndex == TIME::GetFrameContext().frameIndex &&
        frame.camera.valid && frame.renderWidth == static_cast<uint32_t>((std::max)(1, sceneColorTarget.GetWidth())) &&
        frame.renderHeight == static_cast<uint32_t>((std::max)(1, sceneColorTarget.GetHeight())) &&
        frame.renderWidth == static_cast<uint32_t>((std::max)(1, sceneDepthTarget.GetWidth())) &&
        frame.renderHeight == static_cast<uint32_t>((std::max)(1, sceneDepthTarget.GetHeight()));
    if (!frameCurrent || !frame.temporalResolveAllowed) {
        RejectTemporalFrame(frame, resources, false);
        return result;
    }

    const RenderDebugOutputRoute& debugRoute = GetRenderDebugOutputRoute();
    const bool debugRequested = debugRoute.temporalVisualization;

    if (debugRoute.active && !debugRequested) {
        (void)MarkTemporalAntiAliasing(resources, false, false);
        (void)CompleteTemporalFrame(frame.view, frame.publication, false);
        return result;
    }

    const bool nativeTaaRequested = IsTemporalAntiAliasingMode(quality.antiAliasingMode) || debugRequested;
    const UPSCALING::StreamlineDlssMode streamlineMode = !debugRoute.bypassTemporalUpscaler
                                                             ? UPSCALING::ResolveStreamlineDlssMode(quality)
                                                             : UPSCALING::StreamlineDlssMode::Off;
    const bool streamlineRequested = streamlineMode != UPSCALING::StreamlineDlssMode::Off;
    const bool superResolutionRequested = UPSCALING::IsStreamlineDlssSuperResolutionMode(streamlineMode);

    result.requested = nativeTaaRequested || streamlineRequested;
    if (!result.requested) {
        (void)MarkTemporalAntiAliasing(resources, false, false);
        (void)CompleteTemporalFrame(frame.view, frame.publication, false);
        return result;
    }

    result.expectedOutputWidth = frame.outputWidth;
    result.expectedOutputHeight = frame.outputHeight;

    const D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv =
        sceneDepthTarget.HasDepth() ? sceneDepthTarget.GetDepthSrvGpu() : D3D12_GPU_DESCRIPTOR_HANDLE{};
    const bool depthReadActive = sceneDepthSrv.ptr != 0 && sceneDepthTarget.BeginDepthShaderRead();
    if (!depthReadActive || !PrepareSceneColorInput(resources, sceneColorTarget)) {
        if (depthReadActive) {
            sceneDepthTarget.EndDepthShaderRead();
        }
        RejectTemporalFrame(frame, resources, nativeTaaRequested);
        return result;
    }

    (void)UpdateTemporalExposure(resources, exposureState);
    TemporalInputs maskInputs{};
    if (!TryBuildTemporalResolveInputs(resources, sceneColorTarget, sceneDepthTarget, maskInputs)) {
        sceneDepthTarget.EndDepthShaderRead();
        RejectTemporalFrame(frame, resources, nativeTaaRequested);
        return result;
    }
    const bool masksWritten = ExecuteTemporalMaskPass(graphics, maskInputs);
    TemporalInputs inputs{};
    if (!TryBuildTemporalResolveInputs(resources, sceneColorTarget, sceneDepthTarget, inputs)) {
        sceneDepthTarget.EndDepthShaderRead();
        RejectTemporalFrame(frame, resources, nativeTaaRequested);
        return result;
    }
    const TaaResolveSettings taaSettings = BuildTaaSettings(quality);

    RenderTarget2D* resolvedTarget = nullptr;
    if (streamlineRequested && masksWritten) {
        resolvedTarget = UPSCALING::ExecuteStreamlineDlss(inputs, streamlineMode);
        if (resolvedTarget != nullptr) {
            // Streamline owns its history. Any older native TAA ping-pong
            // must be invalidated before a future fallback can read it.
            const bool nativeHistoryInvalidated = CommitTemporalHistory(inputs.resourceFrame, false);
            const bool cpuFrameCompleted = CompleteTemporalFrame(inputs.frame.view, inputs.frame.publication, true);
            if (!nativeHistoryInvalidated || !cpuFrameCompleted) {
                resolvedTarget = nullptr;
            } else {
                result.backend = TemporalResolveBackend::Streamline;
                (void)MarkTemporalAntiAliasing(resources, false, false);
            }
        }
    }

    if (streamlineRequested && resolvedTarget == nullptr) {
        UPSCALING::MarkStreamlineDlssFallback();
        resolvedTarget = ExecuteTaaResolvePass(graphics, inputs, taaSettings);
        result.backend = TemporalResolveBackend::TaaFallback;
    } else if (nativeTaaRequested) {
        resolvedTarget = ExecuteTaaResolvePass(graphics, inputs, taaSettings);
        result.backend = TemporalResolveBackend::Taa;
    }

    sceneDepthTarget.EndDepthShaderRead();

    if (resolvedTarget == nullptr || resolvedTarget->GetResource() == nullptr) {
        RejectTemporalFrame(frame, resources, nativeTaaRequested);
        return result;
    }

    result.outputJitterPixels = result.backend == TemporalResolveBackend::Streamline
                                    ? MATH::Vec2{}
                                    : MATH::Vec2{frame.camera.jitter.x, frame.camera.jitter.y};
    result.output = resolvedTarget;
    result.resolved = true;
    result.requiresOutputNormalization =
        superResolutionRequested && (resolvedTarget->GetWidth() != static_cast<int>(frame.outputWidth) ||
                                     resolvedTarget->GetHeight() != static_cast<int>(frame.outputHeight));

    if (debugRequested) {
        RenderTarget2D* debugTarget = GetTemporalDebugRenderTarget(resources);
        if (debugTarget != nullptr && debugTarget->GetResource() != nullptr) {
            result.output = debugTarget;
            result.outputJitterPixels = {frame.camera.jitter.x, frame.camera.jitter.y};
            result.requiresOutputNormalization = false;
        }
    }
    return result;
}

const char* ToString(TemporalResolveBackend backend) {
    switch (backend) {
    case TemporalResolveBackend::Off: return "off";
    case TemporalResolveBackend::Taa: return "taa";
    case TemporalResolveBackend::Streamline: return "streamline";
    case TemporalResolveBackend::TaaFallback: return "taa-fallback";
    default: return "unknown";
    }
}

} // namespace HIKARI::RENDER3D::TEMPORAL
