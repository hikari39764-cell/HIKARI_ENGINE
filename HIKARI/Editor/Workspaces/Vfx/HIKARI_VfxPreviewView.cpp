#include "Editor/Workspaces/Vfx/HIKARI_VfxPreviewSession.h"

#include <vector>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Vfx/Compiler/HIKARI_VfxCompiler.h"
#include "Editor/Views/HIKARI_EditorViewInstance.h"
#include "Render3D/Color/HIKARI_ExposureState.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Environment/HIKARI_EnvironmentRuntime.h"
#include "Scene/Vfx/HIKARI_VfxLightOutput.h"

#include "Editor/Workspaces/Vfx/HIKARI_VfxPreviewBounds.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"

namespace HIKARI::EDITOR {
namespace {
// 確認用の固定露出（晴天の屋外相当）。発光の見え方をシーンの露出に依存させない。
constexpr float kPreviewEv100 = 14.0f;
} // namespace

VfxPreviewSession::VfxPreviewSession()
    : emptySceneSource_(std::make_unique<RENDER3D::GPUDRIVEN::GpuDrivenSceneSource>()),
      previewEnvironment_(std::make_unique<SceneEnvironment>()),
      previewLighting_(std::make_unique<RENDER3D::LIGHTING::SceneLightingSet>()) {
    Camera3D previewCamera = cameraController_.GetCamera();
    previewCamera.SetLookAt({1.5f, 2.0f, -6.0f}, {0.0f, 0.75f, 0.0f});
    cameraController_.ResetFromCamera(previewCamera);
    // 中性プレビュー。背景は一様な色で塗り、主光源は向きと明るさを変えられる固定の太陽。
    EnvironmentProfile& profile = *previewEnvironment_;
    profile.exposure.mode = RENDER3D::COLOR::ExposureMode::Manual;
    profile.exposure.manualEv100 = kPreviewEv100;
    // 天空光と反射は DefaultSky の IBL。画素値 1 が表示の 1 になるよう水平面照度で較正する。
    profile.sky.source = SkySource::Hdri;
    profile.sky.asset = "DefaultSky";
    profile.sky.textureIlluminance = 3.14159265f / RENDER3D::COLOR::ExposureFromEv100(kPreviewEv100);
    profile.sun.mode = SunMode::Fixed;
    profile.sun.showDisk = false;
    profile.moon.enabled = false;
    profile.stars.enabled = false;
    profile.bloom.threshold = 1.0f;
    profile.bloom.intensity = 0.4f;
    profile.bloom.radius = 1.0f;
    profile.bloom.downsampleCount = 4u;
    UpdatePreviewKeyLight();
}

void VfxPreviewSession::UpdatePreviewKeyLight() {
    // 光線の向き {sin(yaw)·0.6, -0.8, cos(yaw)·0.6} を方位（北 0・東 90）と高度で表す。
    EnvironmentProfile& profile = *previewEnvironment_;
    profile.sun.fixedAzimuthDegrees = -previewLightYaw_;
    profile.sun.fixedElevationDegrees = 53.13f;
    profile.sun.illuminance = previewLightIntensity_ / RENDER3D::COLOR::ExposureFromEv100(profile.exposure.manualEv100);
    previewEnvironment_->frame =
        ENVIRONMENT::BuildLightingFrame(profile, ENVIRONMENT::EnvironmentFrameState{}, ENVIRONMENT::GeoLocation{});
    previewNeedsRedraw_ = true;
}

VfxPreviewSession::~VfxPreviewSession() {
    ReleasePreviewNamespace();
    if (activePreviewViewId_.value != 0u) {
        RENDER3D::EDITORVIEW::ClearRequest(activePreviewViewId_);
    }
}

nlohmann::json VfxPreviewSession::DescribeBackgroundPreview() const {
    const auto& input = previewRenderMailbox_.GetCurrent();
    size_t drawableSubmissions = 0;
    if (input)
        for (const auto& emitter : input->emitters)
            if (emitter.drawAfterSimulation)
                ++drawableSubmissions;
    return {{"actualTime", previewElapsedSeconds_},          {"waitingForRender", previewRenderMailbox_.HasPending()},
            {"seekPending", previewSeekTarget_.has_value()}, {"restartPending", previewRestartPending_},
            {"drawableSubmissions", drawableSubmissions},    {"previewMessage", statusMessage_}};
}

bool VfxPreviewSession::FocusPreviewAsset(std::string_view emitterId) {
    if (!source_)
        return false;
    const VfxPreviewBounds bounds = EstimateVfxPreviewBounds(*source_, emitterId, VfxPreviewBoundsPurpose::Framing);
    if (!bounds.valid)
        return false;

    const Camera3D& currentCamera = cameraController_.GetCamera();
    const float halfFov = std::clamp(currentCamera.GetFovYRad() * 0.5f, 0.1f, 1.4f);
    const auto forward = MATH::Normalize(currentCamera.GetTarget() - currentCamera.GetPosition());
    const auto right = MATH::Normalize(MATH::Cross(currentCamera.GetUp(), forward));
    const auto up = MATH::Normalize(MATH::Cross(forward, right));
    const float tanY = std::tan(halfFov);
    const float tanX = tanY * (std::max)(currentCamera.GetAspect(), 0.1f);
    float fitDistance = 1.5f;
    for (unsigned corner = 0; corner < 8; ++corner) {
        const MATH::Vec3 point{corner & 1 ? bounds.maximum.x : bounds.minimum.x,
                               corner & 2 ? bounds.maximum.y : bounds.minimum.y,
                               corner & 4 ? bounds.maximum.z : bounds.minimum.z};
        const auto offset = point - bounds.center;
        const float depth = MATH::Dot(offset, forward);
        fitDistance =
            (std::max)({fitDistance, std::abs(MATH::Dot(offset, right)) * 1.12f / tanX - depth,
                        std::abs(MATH::Dot(offset, up)) * 1.12f / tanY - depth, currentCamera.GetNearZ() - depth});
    }
    const float preferredDistance = fitDistance;
    const float farPlane = (std::max)(100.0f, preferredDistance + bounds.radius * 2.0f + 10.0f);
    cameraController_.GetCamera().SetPerspective(currentCamera.GetFovYRad(), currentCamera.GetAspect(),
                                                 currentCamera.GetNearZ(), farPlane);
    cameraController_.Focus(bounds.center, preferredDistance);
    ++cameraRevision_;
    cameraCutPending_ = true;
    return true;
}

bool VfxPreviewSession::IsBackgroundPreviewSettled() const noexcept {
    return backgroundFrozenPrepared_ && !previewSeekTarget_ && !previewRestartPending_ &&
           !previewRenderMailbox_.HasPending();
}

bool VfxPreviewSession::PrepareBackgroundPreview(EditorViewInstance& view, float timeSeconds, std::string& outMessage) {
    if (!source_) {
        outMessage = "No VFX document is open";
        return false;
    }
    if (!std::isfinite(timeSeconds) || timeSeconds < 0 || timeSeconds > GetPreviewTimelineDuration()) {
        outMessage = "Capture time is outside the document timeline";
        return false;
    }
    auto& camera = cameraController_.GetCamera();
    camera.SetPerspective(camera.GetFovYRad(), view.extent.Aspect(), camera.GetNearZ(), camera.GetFarZ());
    // 明示的な撮影はドラッグ用 debounce を待たない。背景では時間を進めないため。
    previewRestartDelaySeconds_ = 0.0f;
    if (!previewPlaying_ && !PlayPreview(outMessage))
        return false;
    if (!SeekPreview(timeSeconds, outMessage))
        return false;
    PausePreview(true);
    backgroundFrozenPrepared_ = false;
    previewNeedsRedraw_ = true;
    return true;
}

void VfxPreviewSession::SubmitPreviewRequest(EditorViewInstance& previewView, bool canvasVisible) {
    if (activePreviewViewId_.value != 0u && activePreviewViewId_.value != previewView.renderViewId.value) {
        RENDER3D::EDITORVIEW::ClearRequest(activePreviewViewId_);
    }
    activePreviewViewId_ = previewView.renderViewId;
    if (!source_ || !canvasVisible) {
        RENDER3D::EDITORVIEW::ClearRequest(previewView.renderViewId);
        return;
    }
    RENDER3D::EDITORVIEW::EditorInteractiveViewRequest request{};
    // 無変更の判定は view scheduler が所有する。停止中も extent/露出/guide
    // の変更を渡し、内容が変わったときだけ再描画する。
    if (previewNeedsRedraw_)
        ++previewContentRevision_;
    previewNeedsRedraw_ = false;
    request.contentRevision = previewContentRevision_;
    request.viewId = previewView.renderViewId;
    request.purpose = RENDER3D::RenderViewPurpose::ToolPreview;
    request.cameraFrame.camera = cameraController_.GetCamera();
    request.cameraFrame.revision = cameraRevision_;
    request.cameraFrame.cameraCut = cameraCutPending_;
    request.cameraFrame.valid = true;
    request.width = previewView.extent.width;
    request.height = previewView.extent.height;
    request.sceneRevision = previewRenderMailbox_.GetSequence();
    request.visible = canvasVisible;
    request.drawDebug = false;
    // 再生中は連続描画、停止中はカメラ/資産/設定の変更時に更新する。
    request.continuousRealtime = !previewPaused_;
    request.schedulingPriority = 110u;
    request.shadingMode = RENDER3D::EDITORVIEW::EditorInteractiveShadingMode::Lit;
    request.displayExposure = previewView.visualization.displayExposure;
    request.sceneSourceOverride = emptySceneSource_.get();
    request.environmentOverride = previewEnvironment_.get();
    // 主光源は環境の太陽。素材自身の光源は同じ runtime bridge で毎回組み直す。
    previewLighting_->lights.clear();
    std::vector<uint8_t> visibleLayers;
    for (const auto& layer : source_->emitters)
        visibleLayers.push_back(IsEmitterPreviewVisible(layer.id) ? 1u : 0u);
    if (previewPlaying_)
        VFX::AppendVfxLightProviders(playbackService_, previewLighting_->lights, visibleLayers);
    request.lightingOverride = previewLighting_.get();
    request.particleInputOverride = previewRenderMailbox_.GetCurrent();
    request.guideOverlay.showGroundGrid = showPreviewGrid_ && !previewCleanView_;
    request.guideOverlay.showWorldAxes = showPreviewAxes_ && !previewCleanView_;
    request.guideOverlay.gridHalfCount = 12;
    request.guideOverlay.gridSpacing = 0.5f;
    request.guideOverlay.axisLength = 1.25f;
    request.suppressSpatialLightingProviders = true;
    request.useDefaultSkyIbl = true;
    request.backgroundColor = {previewBackdrop_, previewBackdrop_, previewBackdrop_, 1.0f};
    request.spatialAntiAliasing = true;
    RENDER3D::EDITORVIEW::SubmitRequest(request);
    cameraCutPending_ = false;
}

void VfxPreviewSession::TickBackgroundPreview(EditorViewInstance& view, uint64_t frameIndex) {
    AcknowledgePreviewRenderInput();
    previewRestartDelaySeconds_ = 0.0f;
    if (previewSeekTarget_ || previewRestartPending_ || previewRenderMailbox_.HasPending()) {
        UpdatePreview(frameIndex, 0.0f);
    } else if (!backgroundFrozenPrepared_) {
        if (playbackService_.PublishFrozenRenderInput())
            (void)PublishPreviewRenderInput();
        backgroundFrozenPrepared_ = true;
    }
    SubmitPreviewRequest(view, true);
}
void VfxPreviewSession::Deactivate(RENDER3D::RenderViewId viewId) {
    RENDER3D::EDITORVIEW::ClearRequest(viewId);
    if (activePreviewViewId_.value && activePreviewViewId_.value != viewId.value)
        RENDER3D::EDITORVIEW::ClearRequest(activePreviewViewId_);
    activePreviewViewId_ = {};
    StopPreview();
    playbackService_.Reset();
    previewRenderMailbox_.Clear();
    previewAccumulatedDeltaSeconds_ = 0.0f;
}

} // namespace HIKARI::EDITOR
