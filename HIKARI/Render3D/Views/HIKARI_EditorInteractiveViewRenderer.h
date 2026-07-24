#pragma once

#if defined(HIKARI_WITH_EDITOR)

#include <cstdint>

#include <d3d12.h>

#include "Render3D/Core/HIKARI_RenderView.h"

namespace HIKARI {
    struct SceneEnvironment;
}

namespace HIKARI::RENDER3D::GPUDRIVEN {
    struct GpuDrivenSceneSource;
}

namespace HIKARI::RENDER3D::EDITORVIEW {

    enum class EditorInteractiveShadingMode : uint8_t {
        Lit,
        Neutral,
        Unlit,
    };

    struct EditorInteractiveViewRequest {
        RenderViewId viewId{};
        ResolvedCameraFrame cameraFrame{};
        uint32_t width = 0;
        uint32_t height = 0;
        uint64_t sceneRevision = 0;
        bool visible = false;
        bool drawDebug = true;
        EditorInteractiveShadingMode shadingMode =
            EditorInteractiveShadingMode::Lit;
        float displayExposure = 1.0f;
        const GPUDRIVEN::GpuDrivenSceneSource* sceneSourceOverride = nullptr;
    };

    struct EditorInteractiveViewOutput {
        RenderViewId viewId{};
        D3D12_GPU_DESCRIPTOR_HANDLE colorSrv{};
        uint32_t width = 0;
        uint32_t height = 0;
        uint64_t sceneRevision = 0;
        uint64_t outputRevision = 0;
        bool ready = false;
    };

    void SubmitRequest(const EditorInteractiveViewRequest& request);
    void ClearRequest(RenderViewId viewId);
    bool RenderPending(
        const SceneEnvironment& environment,
        uint64_t currentSceneRevision);
    EditorInteractiveViewOutput GetOutput(RenderViewId viewId);
    void Shutdown();

} // namespace HIKARI::RENDER3D::EDITORVIEW

#endif
