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
        void RejectTemporalFrame(bool nativeTaaRequested) {
            MarkTemporalAntiAliasing(nativeTaaRequested, false);
            ResetTemporalFrameHistory(TemporalHistoryResetReason::ExplicitReset);
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
    }

    TemporalResolveStageResult ExecuteTemporalResolveStage(
        RenderTarget2D& sceneColorTarget,
        RenderTarget2D& sceneDepthTarget,
        const RenderQualitySettings& quality,
        float exposure) {

        TemporalResolveStageResult result{};
        result.output = &sceneColorTarget;

        const RenderDebugOutputRoute& debugRoute =
            GetRenderDebugOutputRoute();
        const bool debugRequested = debugRoute.temporalVisualization;

        if (debugRoute.active && !debugRequested) {
            MarkTemporalAntiAliasing(false, false);
            return result;
        }

        const bool nativeTaaRequested =
            IsTemporalAntiAliasingMode(quality.antiAliasingMode) ||
            debugRequested;
        const UPSCALING::StreamlineDlssMode streamlineMode =
            !debugRoute.bypassTemporalUpscaler
                ? UPSCALING::ResolveStreamlineDlssMode(quality)
                : UPSCALING::StreamlineDlssMode::Off;
        const bool streamlineRequested =
            streamlineMode != UPSCALING::StreamlineDlssMode::Off;
        const bool superResolutionRequested =
            UPSCALING::IsStreamlineDlssSuperResolutionMode(streamlineMode);

        result.requested = nativeTaaRequested || streamlineRequested;
        if (!result.requested) {
            MarkTemporalAntiAliasing(false, false);
            return result;
        }

        const TemporalFrameState& frame = GetCurrentTemporalFrameState();
        result.expectedOutputWidth = frame.outputWidth;
        result.expectedOutputHeight = frame.outputHeight;

        const bool frameCurrent =
            frame.frameIndex == TIME::GetFrameContext().frameIndex &&
            frame.camera.valid &&
            frame.renderWidth ==
                static_cast<uint32_t>((std::max)(1, sceneColorTarget.GetWidth())) &&
            frame.renderHeight ==
                static_cast<uint32_t>((std::max)(1, sceneColorTarget.GetHeight())) &&
            frame.renderWidth ==
                static_cast<uint32_t>((std::max)(1, sceneDepthTarget.GetWidth())) &&
            frame.renderHeight ==
                static_cast<uint32_t>((std::max)(1, sceneDepthTarget.GetHeight()));
        if (!frameCurrent || !frame.temporalResolveAllowed) {
            RejectTemporalFrame(nativeTaaRequested);
            return result;
        }

        const D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv =
            sceneDepthTarget.HasDepth()
                ? sceneDepthTarget.GetDepthSrvGpu()
                : D3D12_GPU_DESCRIPTOR_HANDLE{};
        const bool depthReadActive =
            sceneDepthSrv.ptr != 0 && sceneDepthTarget.BeginDepthShaderRead();
        if (!depthReadActive || !PrepareSceneColorInput(sceneColorTarget)) {
            if (depthReadActive) {
                sceneDepthTarget.EndDepthShaderRead();
            }
            RejectTemporalFrame(nativeTaaRequested);
            return result;
        }

        (void)UpdateTemporalExposure(exposure);
        const TemporalInputs inputs = BuildTemporalInputs(
            sceneColorTarget,
            sceneDepthTarget);
        const bool masksWritten = ExecuteTemporalMaskPass(inputs);
        const TaaResolveSettings taaSettings = BuildTaaSettings(quality);

        RenderTarget2D* resolvedTarget = nullptr;
        if (streamlineRequested && masksWritten) {
            resolvedTarget = UPSCALING::ExecuteStreamlineDlss(inputs, streamlineMode);
            if (resolvedTarget != nullptr) {
                result.backend = TemporalResolveBackend::Streamline;
                MarkTemporalAntiAliasing(false, false);
            }
        }

        if (streamlineRequested && resolvedTarget == nullptr) {
            UPSCALING::MarkStreamlineDlssFallback();
            resolvedTarget = ExecuteTaaResolvePass(inputs, taaSettings);
            result.backend = TemporalResolveBackend::TaaFallback;
        } else if (nativeTaaRequested) {
            resolvedTarget = ExecuteTaaResolvePass(inputs, taaSettings);
            result.backend = TemporalResolveBackend::Taa;
        }

        sceneDepthTarget.EndDepthShaderRead();

        if (resolvedTarget == nullptr || resolvedTarget->GetResource() == nullptr) {
            RejectTemporalFrame(nativeTaaRequested);
            return result;
        }

        result.output = resolvedTarget;
        result.resolved = true;
        result.requiresOutputNormalization =
            superResolutionRequested &&
            (resolvedTarget->GetWidth() != static_cast<int>(frame.outputWidth) ||
                resolvedTarget->GetHeight() != static_cast<int>(frame.outputHeight));

        if (debugRequested) {
            RenderTarget2D* debugTarget = GetTemporalDebugRenderTarget();
            if (debugTarget != nullptr && debugTarget->GetResource() != nullptr) {
                result.output = debugTarget;
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
