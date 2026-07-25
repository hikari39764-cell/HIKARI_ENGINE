#pragma once

#include "Gfx/HIKARI_Dx12Core.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"
#include "Runtime/Host/HIKARI_HostBootstrapConfig.h"

namespace HIKARI::SERVICES {
    // Transitional handles for render consumers pending explicit context injection.
    // The actual storage remains privately owned by RuntimeServiceState.
    extern GFX::Dx12Core& gCore;
    extern GFX::Context& gCtx;

    struct FrameSceneResolutionPlan {
        RENDER3D::RenderResolution render{};
        RENDER3D::RenderResolution output{};
        RENDER3D::UPSCALING::StreamlineDlssMode streamlineMode =
            RENDER3D::UPSCALING::StreamlineDlssMode::Off;
    };

    enum class FramePresentationDestination {
        BackBuffer,
        EditorViewport
    };

    void SetEditorGameViewportSize(int width, int height, bool visible);
    void UpdateGpuContexts();
    void QueueWindowResize(int width, int height);
    void ClearPendingWindowResize();
    bool ApplyPendingWindowResize();
    bool ApplyWindowPresentationSettings();
    FrameSceneResolutionPlan ResolveFrameSceneResolutionPlan();
    void ApplyFrameSceneCaptureSize();

    bool BeginFrame(
        const RUNTIME::HostBootstrapConfig& cfg = {});
    bool PrepareFramePresentation(
        FramePresentationDestination destination =
            FramePresentationDestination::EditorViewport);
    bool EndFrame();
} // namespace HIKARI::SERVICES
