#pragma once

#if defined(HIKARI_WITH_EDITOR)

#include <cstdint>
#include <string_view>

#include <d3d12.h>

#include "Render3D/Core/HIKARI_RenderView.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"

namespace HIKARI {
    struct SceneEnvironment;
    class ModelManager;
    class SkyManager;
}

namespace HIKARI::RENDER3D::GPUDRIVEN {
    struct GpuDrivenSceneSource;
}

namespace HIKARI::RENDER3D::EDITORVIEW {

    enum class EditorInteractiveShadingMode : uint8_t {
        Lit,
        Neutral,
        NeutralClay,
        Unlit,
    };

    enum class EditorInteractiveViewStatus : uint8_t {
        Idle,
        Pending,
        Ready,
        Failed,
    };

    enum class EditorInteractiveViewFailure : uint8_t {
        None,
        RenderTargetUnavailable,
        MeshRendererUnavailable,
        MaterialFrameUnavailable,
        FrameResourcesUnavailable,
        SceneResidencyUnavailable,
        StylizedSurfacePassFailed,
        ForwardPassFailed,
        StylizedGeometryPassFailed,
        StylizedResolveFailed,
        DisplayResolveFailed,
        OutputDescriptorUnavailable,
    };

    struct EditorInteractiveViewRequest {
        RenderViewId viewId{};
        RenderViewPurpose purpose = RenderViewPurpose::EditorScene;
        ResolvedCameraFrame cameraFrame{};
        uint32_t width = 0;
        uint32_t height = 0;
        uint64_t sceneRevision = 0;
        bool visible = false;
        bool drawDebug = true;
        uint32_t schedulingPriority = 0u;
        EditorInteractiveShadingMode shadingMode =
            EditorInteractiveShadingMode::Lit;
        float displayExposure = 1.0f;
        const GPUDRIVEN::GpuDrivenSceneSource* sceneSourceOverride = nullptr;
        const SceneEnvironment* environmentOverride = nullptr;
        const LIGHTING::SceneLightingSet* lightingOverride = nullptr;
        bool suppressSpatialLightingProviders = false;
        bool useDefaultSkyIbl = false;
        // Spatial AA is applied after the shared display resolve. It is
        // intentionally opt-in so Scene views keep their product AA policy.
        bool spatialAntiAliasing = false;
    };

    struct EditorInteractiveViewOutput {
        RenderViewId viewId{};
        D3D12_GPU_DESCRIPTOR_HANDLE colorSrv{};
        uint32_t width = 0;
        uint32_t height = 0;
        uint64_t sceneRevision = 0;
        uint64_t outputRevision = 0;
        EditorInteractiveViewStatus status =
            EditorInteractiveViewStatus::Idle;
        EditorInteractiveViewFailure failure =
            EditorInteractiveViewFailure::None;
        bool ready = false;
    };

    struct EditorInteractiveSchedulerStats {
        uint64_t scheduleSerial = 0u;
        uint64_t renderedViewId = 0u;
        uint32_t renderableViewCount = 0u;
    };

    void SubmitRequest(const EditorInteractiveViewRequest& request);
    void ClearRequest(RenderViewId viewId);
    void RequestPreviewCapture(
        RenderViewId viewId,
        const std::string& outputPath);
    bool PollPreviewCapture(
        const std::string& outputPath,
        std::string& outError);
    bool RenderPending(
        const SceneEnvironment& environment,
        const LIGHTING::SceneLightingSet& lighting,
        uint64_t currentSceneRevision,
        ModelManager& modelManager,
        SkyManager& skyManager);
    EditorInteractiveViewOutput GetOutput(RenderViewId viewId);
    std::string_view GetEditorInteractiveViewFailureMessage(
        EditorInteractiveViewFailure failure) noexcept;
    EditorInteractiveSchedulerStats GetSchedulerStats();
    void Shutdown();

} // namespace HIKARI::RENDER3D::EDITORVIEW

#endif
