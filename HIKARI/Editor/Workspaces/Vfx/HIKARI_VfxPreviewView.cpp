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
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Vfx/HIKARI_VfxLightOutput.h"

#include "Editor/Workspaces/Vfx/HIKARI_VfxPreviewBounds.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"

namespace HIKARI::EDITOR {
VfxPreviewSession::VfxPreviewSession()
    : emptySceneSource_(std::make_unique<RENDER3D::GPUDRIVEN::GpuDrivenSceneSource>()),
      previewEnvironment_(std::make_unique<SceneEnvironment>()),
      previewLighting_(std::make_unique<RENDER3D::LIGHTING::SceneLightingSet>()) {
    Camera3D previewCamera = cameraController_.GetCamera();
    previewCamera.SetLookAt({1.5f, 2.0f, -6.0f}, {0.0f, 0.75f, 0.0f});
    cameraController_.ResetFromCamera(previewCamera);
    // 中性プレビューの発光基準。シーンの任意の露出設定に依存させない。
    previewEnvironment_->bloom.threshold = 1.0f;
    previewEnvironment_->bloom.intensity = 0.4f;
    previewEnvironment_->bloom.radius = 1.0f;
    previewEnvironment_->bloom.downsampleCount = 4u;
    previewEnvironment_->sky.enabled = true;
    previewEnvironment_->sky.mode = SkyMode::Gradient;
    previewEnvironment_->sky.zenithColor = {0.018f, 0.026f, 0.042f};
    previewEnvironment_->sky.horizonColor = {0.065f, 0.082f, 0.105f};
    previewEnvironment_->sky.groundColor = {0.012f, 0.016f, 0.022f};
    previewEnvironment_->sky.showSunDisk = false;
    previewEnvironment_->sky.reflectionIntensity = 1.0f;
    RENDER3D::LIGHTING::SceneLightProvider key{};
    key.id = "vfx.preview.key";
    key.enabled = true;
    key.type = RENDER3D::LIGHTING::SceneLightType::Directional;
    key.direction = {-0.4f, -0.8f, 0.4f};
    key.intensity = previewLightIntensity_;
    previewLighting_->lights.push_back(key);
}

VfxPreviewSession::~VfxPreviewSession() {
    ReleasePreviewNamespace();
#if defined(HIKARI_WITH_EDITOR)
    if (activePreviewViewId_.value != 0u) {
        RENDER3D::EDITORVIEW::ClearRequest(activePreviewViewId_);
    }
#endif
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
#if defined(HIKARI_WITH_EDITOR)
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
    // 作業用の主光源を保ち、素材自身の光源は同じ runtime bridge で再構成する。
    previewLighting_->lights.resize(1);
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
    request.spatialAntiAliasing = true;
    RENDER3D::EDITORVIEW::SubmitRequest(request);
    cameraCutPending_ = false;
#else
    (void)previewView;
    (void)canvasVisible;
#endif
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
#if defined(HIKARI_WITH_EDITOR)
    RENDER3D::EDITORVIEW::ClearRequest(viewId);
    if (activePreviewViewId_.value && activePreviewViewId_.value != viewId.value)
        RENDER3D::EDITORVIEW::ClearRequest(activePreviewViewId_);
#else
    (void)viewId;
#endif
    activePreviewViewId_ = {};
    StopPreview();
    playbackService_.Reset();
    previewRenderMailbox_.Clear();
    previewAccumulatedDeltaSeconds_ = 0.0f;
}

} // namespace HIKARI::EDITOR
