#pragma once
#include <cstdint>
#include <filesystem>

#include "HIKARI/Project/CameraViewSnapshot.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"
#include "Runtime/Host/HIKARI_HostBootstrapConfig.h"

namespace HIKARI::RENDER3D {
struct ResolvedCameraFrame;
}

namespace HIKARI::SERVICES {
namespace detail {
void SubmitPrimaryViewCameraFrame(const RENDER3D::ResolvedCameraFrame& cameraFrame, uint64_t sourceCameraObjectHandle,
                                  GameProjectPrimaryViewSource source, uint64_t worldIdentity,
                                  uint64_t frameIndex) noexcept;
bool TryGetLatestPrimaryViewSnapshot(uint64_t worldIdentity, GameProjectCameraViewSnapshot& outSnapshot) noexcept;
} // namespace detail

struct FrameSceneResolutionPlan {
    RENDER3D::RenderResolution render{};
    RENDER3D::RenderResolution output{};
    RENDER3D::UPSCALING::StreamlineDlssMode streamlineMode = RENDER3D::UPSCALING::StreamlineDlssMode::Off;
};

enum class FramePresentationDestination { BackBuffer, EditorViewport };

void SetEditorGameViewportSize(int width, int height, bool visible);
bool IsEditorGameViewportVisible() noexcept;
void UpdateGpuContexts();
void QueueWindowResize(int width, int height);
void ClearPendingWindowResize();
bool ApplyPendingWindowResize();
bool ApplyWindowPresentationSettings();
bool QueueRenderQualitySettings(const RENDER3D::RenderQualitySettings& settings);
// 今の画質が、フレームの頭の同期（SynchronizeRenderQualityRuntime）で実行時へ反映済みか。設定を変えた直後は、
// 次のフレームの頭までは偽。
bool IsRenderQualityApplied();
FrameSceneResolutionPlan ResolveFrameSceneResolutionPlan();
void ApplyFrameSceneCaptureSize();

bool BeginFrame(const RUNTIME::HostBootstrapConfig& cfg = {});
bool PrepareFramePresentation(FramePresentationDestination destination = FramePresentationDestination::EditorViewport);
bool EndFrame();
bool RequestFrameCapture(const std::filesystem::path& output);
} // namespace HIKARI::SERVICES
