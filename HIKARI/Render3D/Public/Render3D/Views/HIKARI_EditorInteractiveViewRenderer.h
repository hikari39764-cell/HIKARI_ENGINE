#pragma once

#if defined(HIKARI_WITH_EDITOR)

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include <d3d12.h>

#include "Render3D/Core/HIKARI_RenderView.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"

namespace HIKARI {
class AssetRegistry;
struct SceneEnvironment;
class SkyManager;
} // namespace HIKARI

namespace HIKARI::RENDER3D::PARTICLES {
struct ParticleRenderInput;
}

namespace HIKARI::RENDER3D::FLUID {
struct FluidGlowReport;
}

namespace HIKARI::RENDER3D::MATERIAL {
struct MaterialExternalValueSnapshot;
}

namespace HIKARI::RENDER3D::GPUDRIVEN {
struct GpuDrivenSceneSource;
}

namespace HIKARI::RENDER3D::WORLD_SPRITES {
struct WorldSpriteFrameSource;
}

namespace HIKARI::RENDER3D::WORLD_TEXT {
struct WorldTextFrameSource;
}

namespace HIKARI::RENDER3D::EDITORVIEW {

// Tool previews own these guides per view. They are expanded directly by
// the interactive-view renderer and never enter the global Scene debug
// submission list.
struct EditorInteractiveViewGuideOverlay {
    bool showGroundGrid = false;
    bool showWorldAxes = false;
    int32_t gridHalfCount = 10;
    float gridSpacing = 1.0f;
    float gridThicknessPixels = 1.0f;
    float axisLength = 1.0f;
    float axisThicknessPixels = 2.0f;
};

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
    // Workspace-owned, non-scene preview inputs such as local lighting
    // increment this revision when their borrowed data changes in place.
    uint64_t contentRevision = 0;
    bool visible = false;
    bool drawDebug = true;
    // Continuous tool views (for example VFX playback) must be sampled
    // every presented frame. They share the same renderer as runtime, but
    // must not be time-sliced with static inspector thumbnails.
    bool continuousRealtime = false;
    uint32_t schedulingPriority = 0u;
    EditorInteractiveShadingMode shadingMode = EditorInteractiveShadingMode::Lit;
    float displayExposure = 1.0f;
    const GPUDRIVEN::GpuDrivenSceneSource* sceneSourceOverride = nullptr;
    const SceneEnvironment* environmentOverride = nullptr;
    const LIGHTING::SceneLightingSet* lightingOverride = nullptr;
    std::shared_ptr<const PARTICLES::ParticleRenderInput> particleInputOverride{};
    // particleInputOverride の流体域の発光の回読を受け取る先（特效の预览が自分の再生側へ返す。火の光源が読む）。
    std::shared_ptr<std::vector<FLUID::FluidGlowReport>> fluidGlowReports{};
    EditorInteractiveViewGuideOverlay guideOverlay{};
    bool suppressSpatialLightingProviders = false;
    bool useDefaultSkyIbl = false;
    // a > 0 の時、空の代わりに rgb（表示参照の値）の一様な背景を塗る。VFX 確認用の暗・灰・明の背景。
    std::array<float, 4> backgroundColor{};
    // Spatial AA is applied after the shared display resolve. It is
    // intentionally opt-in so Scene views keep their product AA policy.
    bool spatialAntiAliasing = false;
};

struct EditorInteractiveViewOutput {
    RenderViewId viewId{};
    D3D12_GPU_DESCRIPTOR_HANDLE colorSrv{};
    uint32_t width = 0;
    uint32_t height = 0;
    // sceneRevision is the last successfully completed revision retained
    // by this receipt. requestedRevision describes a newer request without
    // invalidating that last-completed image while status is Pending.
    uint64_t sceneRevision = 0;
    uint64_t requestedRevision = 0u;
    uint64_t outputRevision = 0;
    EditorInteractiveViewStatus status = EditorInteractiveViewStatus::Idle;
    EditorInteractiveViewFailure failure = EditorInteractiveViewFailure::None;
    bool ready = false;
};

struct EditorInteractiveSchedulerStats {
    uint64_t scheduleSerial = 0u;
    uint64_t renderedViewId = 0u;
    uint32_t renderableViewCount = 0u;
};

// All view-state APIs are Editor/presentation-owner-thread only. Pointer
// overrides remain borrowed until their request is replaced or cleared;
// an output is valid only while the corresponding view stays active.
void SubmitRequest(const EditorInteractiveViewRequest& request);
void ClearRequest(RenderViewId viewId);
// Capture submission/polling are thread-safe; view access stays on the
// presentation owner. A ticket always refers to one requested fresh frame.
using PreviewCaptureId = uint64_t;
PreviewCaptureId RequestViewCapture(RenderViewId viewId, const std::string& outputPath);
struct PreviewCaptureResult {
    bool done = false;
    std::string path, error;
    RenderViewId viewId{};
    uint32_t width = 0, height = 0;
    uint64_t outputRevision = 0;
    uint64_t frameIndex = 0;
    uint64_t sceneRevision = 0;
    ResolvedCameraFrame cameraFrame{};
};
PreviewCaptureResult GetViewCaptureResult(PreviewCaptureId id);
void CancelViewCapture(PreviewCaptureId id);
bool RenderPending(const SceneEnvironment& environment, const LIGHTING::SceneLightingSet& lighting,
                   uint64_t currentSceneRevision, SkyManager& skyManager, const AssetRegistry* assetRegistry = nullptr,
                   const MATERIAL::MaterialExternalValueSnapshot* materialExternalValues = nullptr,
                   const WORLD_SPRITES::WorldSpriteFrameSource* worldSpriteFrameSource = nullptr,
                   const WORLD_TEXT::WorldTextFrameSource* worldTextFrameSource = nullptr);
EditorInteractiveViewOutput GetOutput(RenderViewId viewId);
std::string_view GetEditorInteractiveViewFailureMessage(EditorInteractiveViewFailure failure) noexcept;
EditorInteractiveSchedulerStats GetSchedulerStats();
void Shutdown();

} // namespace HIKARI::RENDER3D::EDITORVIEW

#endif
