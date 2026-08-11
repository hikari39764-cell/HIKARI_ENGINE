#pragma once

#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"
#include "Runtime/Host/HIKARI_HostBootstrapConfig.h"

namespace HIKARI::SERVICES {
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
    bool IsEditorGameViewportVisible() noexcept;
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
