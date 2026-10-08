#include "Audio/HIKARI_Audio.h"
#include "Core/HIKARI_StartupTimer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Document/Internal/HIKARI_DocumentSceneState.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include "Render3D/Color/HIKARI_ViewExposure.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Runtime/Presentation/HIKARI_FramePresentationService.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectSessionRuntimeExtension.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingCatalogJson.h"
#include "Runtime/Binding/Adapters/HIKARI_RuntimeMaterialProjection.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Core/Presentation/HIKARI_LogicalDisplayDefaults.h"
#include "Core/HIKARI_TimeService.h"
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
#include "Render3D/Vfx/HIKARI_ParticleGpuDrivenPass.h"
#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"
#if defined(HIKARI_WITH_EDITOR)
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Render3D/Views/HIKARI_EditorViewCapture.h"
#endif
#include "Render3D/Lighting/HIKARI_Lightning.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Lighting/HIKARI_PanoramaSkyLayerStage.h"
#include "Render3D/Lighting/HIKARI_LocalLightList.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Scene/Sequencer/Drivers/HIKARI_CameraSequenceTrackDriver.h"
#include "Scene/Sequencer/Drivers/HIKARI_PresentationCueDriver.h"
#include "Vfx/Post/HIKARI_PostSystem.h"
#include "Vfx/Post/HIKARI_ActionPostProfile.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Scene/Capture/HIKARI_DocumentSceneCaptureIntegration.h"
#include "Scene/Vfx/HIKARI_VfxLightOutput.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"
#include "Scene/Vfx/HIKARI_VfxScreenOutput.h"
#include "Scene/Ui/HIKARI_UiSceneSystem.h"
#include "Scene/Capture/HIKARI_SceneCaptureReadability.h"
#include "Scene/Lighting/HIKARI_SceneLightingSet.h"

namespace HIKARI {
namespace {
// シーンの描画の様式を解く。標準の材質の光の受け方はこのフレームの描画入力へ、後処理の層は全体効果へ渡す。
// 様式や層のプロファイルを解決できなければ、物理の受け方と、その層を飛ばした後処理になる（エディターの様式の欄が
// 理由を示す）。
void ApplyRenderStyle(std::string_view styleId, const RENDERSTYLE::RenderStyleAssetStore& styles,
                      const POST::PostAssetStore& posts, const AssetRegistry& registry, SceneEnvironment& environment) {
    std::string error;
    std::vector<POST::ResolvedPostLayer> layers;
    environment.styleShading = {};
    if (const auto style = styles.Load(styleId, error)) {
        environment.styleShading = style->shading;
        for (const RENDERSTYLE::RenderStylePostLayer& layer : style->post) {
            if (!layer.enabled) {
                continue;
            }
            const auto profile = posts.LoadProfile(layer.profileId, error);
            const auto effect = profile ? posts.LoadEffect(profile->effectId, error) : nullptr;
            if (effect) {
                layers.push_back({profile->effectId, effect, POST::ResolvePostParameters(*effect, profile.get())});
            }
        }
    }
    POST::PostSystem::SetGlobalEffects(layers, &registry);
}

void PrepareActionPost(World& world, bool playActive, const POST::PostAssetStore& store,
                       const AssetRegistry& registry) {
    auto* reactions = world.Services().Find<VFX::ActionPostReactionService>();
    const uint64_t worldId = world.GetRuntimeIdentity();
    if (!reactions) {
        POST::PostSystem::ClearActionPostFrame(worldId);
        return;
    }
    std::optional<VFX::ActionPostReactionService::PostProfileFrame> winner;
    const auto frame = TIME::GetFrameContext().frameIndex;
    if (!reactions->FreezePostProfile(frame, winner))
        return;
    if (!playActive || TIME::IsPaused() || !winner) {
        POST::PostSystem::ClearActionPostFrame(worldId);
        return;
    }
    std::string error;
    const auto profile = store.LoadProfile(winner->profileId, error);
    const auto effect = profile ? store.LoadEffect(profile->effectId, error) : nullptr;
    // 行動の後処理は HDR の段階で特性マスクだけを受け取る（画面の曲げ・深度・表示の色の効果は全体後処理の層で使う）。
    if (!effect || effect->screenWarp || effect->requiresSceneDepth || effect->phase != POST::PostEffectPhase::Scene ||
        !POST::ValidatePostProfileValues(*profile, *effect, error)) {
        POST::PostSystem::ClearActionPostFrame(worldId);
        HIKARI_LOG_ERROR("[ActionPost] Profile unavailable or incompatible: " + winner->profileId + " " + error);
        return;
    }
    const auto values = POST::ResolveActionPostParameters(*effect, *profile, winner->request);
    (void)POST::PostSystem::SetActionPostFrame(worldId, frame, profile->effectId, effect, values, registry);
}

bool TryUnprojectClipCorner(const MATH::Mat4& invViewProj, float x, float y, float z, MATH::Vec3& out) {
    const MATH::Vec4 clip{x, y, z, 1.0f};
    const MATH::Vec4 world = invViewProj.TransformPoint(clip);
    if (std::abs(world.w) < 1.0e-6f || !MATH::IsFinite(world)) {
        return false;
    }

    const float invW = 1.0f / world.w;
    out = {world.x * invW, world.y * invW, world.z * invW};
    return true;
}

void SubmitFrozenCullingCameraDebugFrustum() {
    if (!MESHRENDERER::IsGpuDrivenCullingCameraFrozen()) {
        return;
    }

    const MESHRENDERER::CameraCB* cullingCamera = MESHRENDERER::GetGpuDrivenCullingCameraConstants();
    if (cullingCamera == nullptr) {
        return;
    }

    constexpr std::array<std::array<float, 3>, 8> kClipCorners{{
        {-1.0f, -1.0f, 0.0f},
        {1.0f, -1.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},
        {-1.0f, 1.0f, 0.0f},
        {-1.0f, -1.0f, 1.0f},
        {1.0f, -1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f},
        {-1.0f, 1.0f, 1.0f},
    }};

    std::array<MATH::Vec3, 8> corners{};
    for (size_t i = 0; i < kClipCorners.size(); ++i) {
        const auto& c = kClipCorners[i];
        if (!TryUnprojectClipCorner(cullingCamera->invViewProj, c[0], c[1], c[2], corners[i])) {
            return;
        }
    }

    constexpr std::array<std::array<int, 2>, 12> kEdges{{
        {0, 1},
        {1, 2},
        {2, 3},
        {3, 0},
        {4, 5},
        {5, 6},
        {6, 7},
        {7, 4},
        {0, 4},
        {1, 5},
        {2, 6},
        {3, 7},
    }};
    constexpr unsigned int kNearColor = 0xFFD166FFu;
    constexpr unsigned int kFrustumColor = 0x37C8FFFFu;
    constexpr unsigned int kRayColor = 0x8F6AFFFFu;
    for (size_t i = 0; i < kEdges.size(); ++i) {
        const auto& edge = kEdges[i];
        RENDERER3D::DEBUG::SubmitStroke3D({corners[static_cast<size_t>(edge[0])], corners[static_cast<size_t>(edge[1])],
                                           i < 4 ? kNearColor : kFrustumColor, RENDERER3D::DEBUG::DebugDepthMode::XRay,
                                           1.8f});
    }

    const MATH::Vec3 cullingPosition{cullingCamera->cameraPos.x, cullingCamera->cameraPos.y,
                                     cullingCamera->cameraPos.z};
    for (size_t i = 0; i < 4; ++i) {
        RENDERER3D::DEBUG::SubmitStroke3D(
            {cullingPosition, corners[i], kRayColor, RENDERER3D::DEBUG::DebugDepthMode::XRay, 1.4f});
    }
}
} // namespace

DocumentSceneBase::DocumentSceneBase(std::string sceneId) : state_(std::make_unique<DocumentSceneState>()) {
    state_->identity.sceneId = std::move(sceneId);

    (void)state_->camera.sequencePlayback.RegisterDriver(std::make_unique<SEQUENCER::CameraSequenceTrackDriver>(
        state_->camera.director, state_->runtime.world, state_->camera.editorCamera, &state_->camera.presentation));
    (void)state_->camera.sequencePlayback.RegisterDriver(
        std::make_unique<SEQUENCER::PresentationCueDriver>(state_->runtime.world, state_->runtime.vfxPlaybackService));
}

DocumentSceneBase::~DocumentSceneBase() {
    SERVICES::GetInputService().SetDeveloperInputCaptured(false);
    state_->runtime.debugService.Reset(state_->runtime.world);
}

void DocumentSceneBase::OnEnter() {
    CORE::StartupTimer sceneTimer("Scene.enter");
    state_->runtime.initialized = true;
    state_->camera.editorCamera.SetPerspective(60.0f * std::numbers::pi_v<float> / 180.0f,
                                               static_cast<float>(PRESENTATION::kDefaultLogicalWidth) /
                                                   static_cast<float>(PRESENTATION::kDefaultLogicalHeight),
                                               Camera3D::kDefaultNearZ, Camera3D::kDefaultFarZ);
    state_->camera.gameplayCamera = state_->camera.editorCamera;
    state_->camera.debugCamera.Reset({0.0f, 2.0f, -6.0f}, 0.0f, 0.0f);
    state_->runtime.fixedStepClock.SetSettings(FixedStepSettings{TIME::GetFixedDeltaSeconds(), 3, 0.25f});
    state_->runtime.fixedStepClock.Reset();
    if (!RegisterRuntimeFeatures()) {
        HIKARI_LOG_ERROR("[RuntimeFeature] one or more built-in features failed to install");
    }
    if (!ConfigureRuntimeWorldServices()) {
        HIKARI_LOG_ERROR("[RuntimeExtension] world service registration failed");
        OnExit();
        return;
    }

    const bool assetsReady = ReloadAssets();
    if (!assetsReady) {
        HIKARI_LOG_ERROR("[SceneAsset] startup asset publication failed; "
                         "Scene initialization was canceled.");
        OnExit();
        return;
    }
    state_->assets.SequenceAssets().SetAssetDatabase(&state_->assets.Database());
    state_->camera.sequencePlayback.SetAssetStore(&state_->assets.SequenceAssets());
    state_->runtime.dialogueAssets.SetAssetDatabase(&state_->assets.Database());
    if (!OpenStartupSceneAssetWithCurrentAssets()) {
        if (SERVICES::IsStandaloneGameHost()) {
            HIKARI_LOG_ERROR("[SceneAsset] startup Scene failed: " + state_->identity.loadError);
            OnExit();
            return;
        }
        CreateTransientEmptySceneDocument();
        if (!RebuildRuntimeWorld(false, true)) {
            HIKARI_LOG_ERROR("[SceneAsset] transient Scene initialization failed.");
            OnExit();
        }
    }
}
void DocumentSceneBase::OnExit() {
    if (!state_->runtime.initialized) {
        return;
    }
    RUNTIME::GameProjectSessionRuntimeExtension* sessionOwner =
        state_->runtime.world.Services().Find<RUNTIME::GameProjectSessionRuntimeExtension>();
    if (sessionOwner != nullptr) {
        sessionOwner->ExitScene(state_->runtime.world);
    }
    if (UiSceneSystem* uiSystem = state_->runtime.world.Services().Find<UiSceneSystem>())
        uiSystem->ReleaseInputOwnership();
    SERVICES::GetActionTimeDilationService().ReleaseOwner(&state_->runtime.world);
    state_->camera.sequencePlayback.Reset();
    state_->camera.currentCameraSequenceHandle = {};
    state_->camera.rigService.Clear();
    state_->assets.SequenceAssets().Clear();
    state_->runtime.dialogueSessionService.Reset(&state_->runtime.world);
    state_->runtime.dialogueAssets.Clear();
    state_->runtime.systemScheduler.DetachWorld(state_->runtime.world);
    state_->runtime.systemScheduler.Clear();
    state_->runtime.presentationModelService.Clear();
    state_->runtime.localFogService.Clear();
    state_->runtime.sceneCaptureService.Reset();
    MODELRENDERER::ResetSecondarySceneCaptureTarget();
    // RenderSubmissionSystem borrows the document-owned Registry and
    // ModelManager. Clear both before DocumentSceneState is destroyed.
    RenderSubmissionSystem::SetAssetContext(nullptr, {}, nullptr);
    // Components detach while World services (including RuntimeBinding)
    // are still available, so SDK contexts can release owned writers.
    state_->runtime.world.Clear();
    state_->runtime.worldBuilt = false;
    if (sessionOwner != nullptr) {
        sessionOwner->StopGameSession();
    }
    state_->runtime.playActive = false;
    state_->runtime.stopGameProjectSessionAfterWorldDetach = false;
    state_->runtime.world.Services().Clear();
    state_->runtime.worldServicesConfigured = false;
    state_->runtime.terrainSurfaceQueryService.Clear();
    state_->runtime.waterSurfaceQueryService.Clear();
    state_->runtime.navigationWorldService.Clear();
    state_->runtime.animationPoseService.Clear();
    state_->runtime.animationNodeOverrideService.Clear();
    state_->runtime.animationRootMotionService.Clear();
    state_->runtime.animationStateMachineRuntimeService.Clear();
    state_->runtime.animationStateMachineAssets.SetAssetDatabase(nullptr);
    state_->runtime.animationStateMachineAssets.Clear();
    state_->runtime.waterSurfaceQueryService.SetBathymetryProvider(nullptr);
    state_->runtime.waterBathymetryProvider.reset();
    state_->runtime.physicsWorldService.DetachWorld();
    (void)state_->runtime.physicsWorldService.RemoveBackend();
    state_->runtime.materialProjectionSourceSnapshot.reset();
    state_->runtime.materialExternalValueSnapshot.reset();
    state_->runtime.bindingStore.Clear();
    MESHRENDERER::ReleaseParticleNamespace(state_->runtime.vfxPlaybackService.GetPlaybackNamespace());
    state_->runtime.vfxPlaybackService.Reset();
    state_->runtime.vfxPlaybackService.SetProgramResolver({});
    state_->runtime.vfxPlaybackService.SetAttachmentResolver({});
    state_->runtime.vfxPlaybackService.SetReliableRenderDeliveryEnabled(false);
    state_->runtime.combatPresentationService.Reset();
    state_->runtime.combatAfterimageService.Reset();
    state_->runtime.actionPostReactionService.Reset();
    POST::PostSystem::ClearActionPostFrame(state_->runtime.world.GetRuntimeIdentity());
    state_->runtime.fixedStepClock.Reset();
    state_->runtime.initialized = false;
    RuntimeSceneContext::SetCurrentWorld(nullptr);
}
void DocumentSceneBase::Update(float dt) {
    FrameContext frame = HIKARI::TIME::GetFrameContext();
    if (std::abs(state_->runtime.fixedStepClock.GetSettings().stepSeconds - frame.fixedDt) > 1e-6f) {
        FixedStepSettings settings = state_->runtime.fixedStepClock.GetSettings();
        settings.stepSeconds = frame.fixedDt;
        state_->runtime.fixedStepClock.SetSettings(settings);
    }
    state_->runtime.world.BeginFrame(frame.frameIndex);
    state_->runtime.debugService.SetEnabled(state_->runtime.world,
                                            state_->runtime.playActive && state_->runtime.developerDiagnosticsAllowed);
    if (!state_->runtime.playActive)
        SERVICES::GetInputService().SetDeveloperInputCaptured(false);
    NAVIGATION::NavigationWorldService* navigation =
        state_->runtime.world.Services().Find<NAVIGATION::NavigationWorldService>();
    if (state_->runtime.playActive && navigation != nullptr) {
        // One owner-revision gate per frame; individual SDK queries read
        // the immutable prepared Detour snapshot in O(path work).
        (void)navigation->Synchronize(state_->runtime.world, NAVIGATION::SynchronizationMode::Background);
    }
    state_->runtime.bindingStore.BeginFrame(frame.frameIndex);
    state_->camera.rigService.BeginFrame(frame.frameIndex);
    if (state_->runtime.playActive && state_->camera.runtimePreviewCameraActive) {
        state_->camera.runtimePreviewCamera.Update(dt, state_->camera.gameplayCamera, SERVICES::GetInputSnapshot(),
                                                   CameraControlInputContext::RuntimeWindow);
    } else if (UseDebugCamera() && !IsEditorCameraPreviewActive() && !state_->camera.sequencePreview.IsValid()) {
        state_->camera.debugCamera.Update(dt, state_->camera.editorCamera, SERVICES::GetInputSnapshot(),
                                          CameraControlInputContext::EditorViewport);
    }
    state_->runtime.systemScheduler.PreUpdate(state_->runtime.world, frame);
    state_->runtime.world.Update(dt);

    const FixedStepFramePlan fixedPlan = state_->runtime.fixedStepClock.Advance(frame.gameDt);
    frame.fixedDt = fixedPlan.stepSeconds;
    frame.fixedStepsThisFrame = fixedPlan.stepCount;
    frame.fixedInterpolationAlpha = fixedPlan.interpolationAlpha;
    frame.droppedFixedTime = fixedPlan.droppedSeconds;
    TIME::ReportFixedStepFrame(state_->runtime.fixedStepClock.GetCompletedTickCount(), fixedPlan.stepCount,
                               fixedPlan.interpolationAlpha, fixedPlan.droppedSeconds);
    // Update/LateUpdate observe the completed simulation tick for this
    // frame. Fixed callbacks below still receive their exact per-step
    // index through fixedFrame.
    frame.fixedTickIndex = state_->runtime.fixedStepClock.GetCompletedTickCount();
    for (uint32_t stepIndex = 0; stepIndex < fixedPlan.stepCount; ++stepIndex) {
        FrameContext fixedFrame = frame;
        fixedFrame.rawDt = fixedPlan.stepSeconds;
        fixedFrame.gameDt = fixedPlan.stepSeconds;
        fixedFrame.unscaledDt = fixedPlan.stepSeconds;
        fixedFrame.fixedTickIndex = fixedPlan.GetTickIndex(stepIndex);
        fixedFrame.fixedStepIndex = stepIndex;
        fixedFrame.isFixedStep = true;
        state_->runtime.world.BeginFixedStep(fixedFrame.fixedTickIndex);
        state_->runtime.systemScheduler.PreFixedUpdate(state_->runtime.world, fixedFrame);
        state_->runtime.systemScheduler.FixedUpdate(state_->runtime.world, fixedFrame);
        state_->runtime.systemScheduler.PostFixedUpdate(state_->runtime.world, fixedFrame);
    }

    state_->runtime.systemScheduler.Update(state_->runtime.world, frame);
    state_->runtime.systemScheduler.LateUpdate(state_->runtime.world, frame);
    // Play 中の環境はゲーム時間で進む（一時停止とスローに従う）。編集中も天気の遷移・風・雲は実時間で進め、
    // 時刻だけは環境パネルの試し再生の速さで流す。環境パネルを開いているかどうかで空が止まらない。
    if (state_->runtime.playActive) {
        state_->runtime.environment.Advance(frame.gameDt);
    } else {
        state_->runtime.environment.Simulation().SetTimeScale(state_->runtime.environmentPreviewTimeScale);
        state_->runtime.environment.Advance(dt);
    }
    state_->runtime.debugService.Observe(state_->runtime.world);
    (void)state_->runtime.bindingStore.PublishSnapshot();

    if (state_->runtime.playActive) {
        if (IsRuntimeFeatureActive(RuntimeFeatureIds::Cinematics)) {
            state_->camera.sequencePlayback.Update(dt);
        }
        state_->camera.resolvedFrame = state_->camera.director.Resolve(
            state_->runtime.world, state_->camera.gameplayCamera, state_->camera.editorCamera.GetAspect(), dt);
        state_->camera.editorCamera = state_->camera.resolvedFrame.camera;
        state_->camera.gameplayCamera = state_->camera.director.GetControlCamera();
        const auto& camera = state_->camera.resolvedFrame.camera;
        const auto position = camera.GetPosition();
        const auto forward = MATH::Normalize(camera.GetTarget() - position);
        const auto right = MATH::Normalize(MATH::Cross(camera.GetUp(), forward));
        const auto up = MATH::Normalize(MATH::Cross(forward, right));
        AUDIO::UpdateCameraListener(
            {{position.x, position.y, position.z}, {forward.x, forward.y, forward.z}, {up.x, up.y, up.z}});
    } else if (state_->camera.sequencePreview.IsValid()) {
        state_->camera.resolvedFrame = state_->camera.director.Resolve(
            state_->runtime.world, state_->camera.sequencePreviewSnapshot, state_->camera.editorCamera.GetAspect(), dt);
        state_->camera.editorCamera = state_->camera.resolvedFrame.camera;
    } else if (IsEditorCameraPreviewActive()) {
        state_->camera.resolvedFrame =
            state_->camera.director.Resolve(state_->runtime.world, state_->camera.editorCameraPreviewSnapshot,
                                            state_->camera.editorCamera.GetAspect(), dt);
        state_->camera.editorCamera = state_->camera.resolvedFrame.camera;
    } else {
        state_->camera.resolvedFrame.camera = state_->camera.editorCamera;
        state_->camera.resolvedFrame.sourceCameraObjectId = 0;
        state_->camera.resolvedFrame.cameraCut = state_->camera.editorCameraCutPending;
        state_->camera.resolvedFrame.projectionChanged = false;
        state_->camera.resolvedFrame.valid = true;
        state_->camera.editorCameraCutPending = false;
    }
}
void DocumentSceneBase::Render() {
    const std::shared_ptr<const BINDING::Snapshot> publishedBindingSnapshot =
        state_->runtime.bindingStore.GetPublishedSnapshot();
    BINDING::ScopedSnapshot bindingSnapshot(publishedBindingSnapshot);
    if (publishedBindingSnapshot != nullptr) {
        if (state_->runtime.materialProjectionSourceSnapshot != publishedBindingSnapshot) {
            state_->runtime.materialProjectionSourceSnapshot = publishedBindingSnapshot;
            state_->runtime.materialExternalValueSnapshot =
                BINDING::MATERIAL_ADAPTER::BuildMaterialExternalValueSnapshot(*publishedBindingSnapshot);
        }
        (void)BINDING::MATERIAL_ADAPTER::ApplyExternalValuesToRuntimeMaterials(
            state_->runtime.world, *state_->runtime.materialExternalValueSnapshot);
    } else {
        state_->runtime.materialProjectionSourceSnapshot.reset();
        state_->runtime.materialExternalValueSnapshot.reset();
    }
    SCENE_CAPTURE::ApplyPublishedMaterialBinding(state_->runtime.world, state_->runtime.sceneCaptureService);
    int captureW = 0;
    int captureH = 0;
    POST::PostSystem::GetSceneCaptureSize(captureW, captureH);
    if (captureW > 0 && captureH > 0) {
        const float aspect = static_cast<float>(captureW) / static_cast<float>(captureH);
        const bool projectionChanged = std::abs(state_->camera.editorCamera.GetAspect() - aspect) > 1.0e-5f;
        state_->camera.editorCamera.SetAspect(aspect);
        state_->camera.resolvedFrame.camera = state_->camera.editorCamera;
        state_->camera.resolvedFrame.projectionChanged =
            state_->camera.resolvedFrame.projectionChanged || projectionChanged;
        state_->camera.resolvedFrame.valid = true;
    }

#if defined(HIKARI_WITH_EDITOR)
    if (state_->baking.ProcessReflectionProbeBakeJob(*this)) {
        return;
    }
    if (state_->baking.ProcessLightProbeBakeJob(*this)) {
        return;
    }
    if (state_->baking.ProcessLightmapBakeJob(*this)) {
        return;
    }
#endif

    RENDERER3D::DEBUG::Reset();
    MODELRENDERER::ResetFrame();
    SKYRENDERER::Reset();

    // このフレームの描画入力。環境プロファイル + 時刻・天気を毎フレーム解く。
    state_->lighting.environment = state_->runtime.environment.ResolveFrame();
    ApplyRenderStyle(state_->runtime.environment.GetRenderStyleId(), state_->assets.RenderStyleAssets(),
                     state_->assets.PostAssets(), state_->assets.Registry(), state_->lighting.environment);
    PrepareActionPost(state_->runtime.world, state_->runtime.playActive, state_->assets.PostAssets(),
                      state_->assets.Registry());
    auto bloom = state_->lighting.environment.bloom;
    auto tone = state_->lighting.environment.toneMapping;
    auto exposure = state_->lighting.environment.exposure;
    std::vector<SEQUENCER::ScreenEffectSample> screenEffects;
    state_->camera.presentation.Resolve(state_->camera.director, bloom, tone, exposure, screenEffects);
    POST::PostSystem::SetBloomSettings(bloom);
    POST::PostSystem::SetToneMappingSettings(tone);
    POST::PostSystem::SetExposureSettings(exposure);
    // 目の順応は実時間で進める（一時停止やスローでも露出は追従する）。
    POST::PostSystem::AdvanceExposure(TIME::GetFrameContext().unscaledDt,
                                      SKYRENDERER::EstimateExposureIllumination(state_->lighting.environment));
    POST::PostSystem::SetSequenceEffects(screenEffects);

    // UE 風の地面グリッド: 半透明の地面パネルと主線/副線を一括で描画する。
    if (DrawDebugHelpers() && state_->editor.viewportOverlayState.showGridFloor) {
        RENDERER3D::DEBUG::Grid3D grid{};
        grid.halfCount = 50;
        grid.spacing = 1.0f;
        grid.rgba = 0x5A6A7FA8;
        grid.majorLineEvery = 5;
        grid.majorRgba = 0x7F93ABC8;
        grid.fillRgba = 0x15191FA8;
        RENDERER3D::DEBUG::SubmitGrid3D(grid);
    }

    // 純粋なワイヤーフレームグリッドはデバッグ表示として残す。
    if (DrawDebugHelpers() && state_->editor.viewportOverlayState.showGrid) {
        RENDERER3D::DEBUG::Grid3D grid{};
        grid.halfCount = 10;
        grid.spacing = 1.0f;
        RENDERER3D::DEBUG::SubmitGrid3D(grid);
    }

    if (DrawDebugHelpers() && state_->editor.viewportOverlayState.showAxis) {
        RENDERER3D::DEBUG::Axis3D axis{};
        axis.length = 2.5f;
        RENDERER3D::DEBUG::SubmitAxis3D(axis);
    }

    state_->runtime.world.Render();
    const FrameContext& frame = HIKARI::TIME::GetFrameContext();
    RenderSubmissionSystem::SetAssetContext(&state_->assets.Registry(), state_->assets.Database().GetProjectRoot(),
                                            &state_->assets.Models());
    state_->runtime.systemScheduler.PreRender(state_->runtime.world, frame);
    state_->runtime.systemScheduler.Render(state_->runtime.world, frame);
    state_->runtime.systemScheduler.PostRender(state_->runtime.world, frame);
    const auto localFogVolumes = state_->runtime.localFogService.PrepareFrame(
        state_->runtime.world, frame.frameIndex, &state_->identity.document, state_->identity.documentRevision);
    SceneEnvironment activeEnvironment = state_->lighting.environment;
    RENDER3D::LIGHTING::SceneLightingSet sceneLighting =
        LIGHTING_AUTHORING::BuildRuntimeSceneLightingSet(state_->identity.document, state_->runtime.world);
    // VFX の光源出力はそのフレーム限りの一時光源。照明セットを組んだ
    // 直後に足し、方向光の解決より前に確定させる。
    if (const auto* vfxPlayback = state_->runtime.world.Services().Find<VFX::VfxPlaybackService>()) {
        (void)VFX::AppendVfxLightProviders(*vfxPlayback, sceneLighting.lights);
        // 画面空間の歪みはゲームプレイ由来の画面波と同じ表へ入れる。
        if (auto* reactions = state_->runtime.world.Services().Find<VFX::ActionPostReactionService>()) {
            (void)VFX::SubmitVfxScreenWaves(*vfxPlayback, *reactions);
        }
    }
    // 落雷は雷雲の中の一時的な点光源として場面を照らす（影は落とさない）。
    if (const RENDER3D::LIGHTNING::LightningLight lightning =
            RENDER3D::LIGHTNING::ResolveLightningLight(activeEnvironment, state_->camera.editorCamera.GetPosition());
        lightning.active) {
        RENDER3D::LIGHTING::SceneLightProvider light{};
        light.id = "engine.environment.lightning";
        light.enabled = true;
        light.position = lightning.position;
        light.color = lightning.color;
        light.intensity = lightning.intensity;
        light.range = 60000.0f;
        sceneLighting.lights.push_back(light);
    }
    // エディターの表示切り替えは、このフレームの描画入力だけを変える。
    if (DrawDebugHelpers() && state_->editor.viewportPerformanceState.disableSsaoInEditorViewport) {
        activeEnvironment.ambientOcclusion.enabled = false;
    }
    if (!UseEnvironmentLighting()) {
        activeEnvironment.frame.sun.illuminance = 0.0f;
        activeEnvironment.frame.moon.illuminance = 0.0f;
        activeEnvironment.sky.lightIntensity = 0.0f;
        for (auto& light : sceneLighting.lights) {
            light.intensity = 0.0f;
        }
    }

    MESHRENDERER::SetGpuDrivenCullingCameraFreezeEnabled(DrawDebugHelpers() &&
                                                         state_->editor.viewportDebugViewState.freezeCullingCamera);
    SHADOW::SetDebugFrustumVisible(DrawDebugHelpers() && state_->editor.viewportDebugViewState.showShadowCascades);
    const Camera3D& renderCamera = state_->camera.editorCamera;

    const GFX::Context graphics = GFX::GetCurrentContext();
    const bool renderPrimaryView = !SERVICES::ShouldProduceEditorUiFrame() || SERVICES::IsEditorGameViewportVisible();
    if (renderPrimaryView) {
        // 主ビューの HDR へ書く間は、このフレームに固定した露出（P = E）を使う。
        const RENDER3D::COLOR::ScopedViewExposure primaryExposure(POST::PostSystem::GetExposureState());
        SKYRENDERER::Render(graphics, renderCamera, activeEnvironment, state_->assets.Models(), state_->lighting.sky);
        (void)RENDER3D::PANORAMA_SKY::RenderPanoramaSkyLayer(graphics, renderCamera, activeEnvironment.sky,
                                                             state_->lighting.sky);
        if (DrawDebugHelpers()) {
            state_->runtime.localFogService.DrawSelectedBounds(state_->runtime.world,
                                                               state_->editor.selectedGizmoObjectId.value);
            state_->editor.componentGizmoRenderer.SubmitWorldGizmos(
                state_->runtime.world, state_->editor.componentGizmoState, state_->editor.selectedGizmoObjectId,
                renderCamera.GetAspect(), renderCamera.GetPosition());
        }
        RENDER3D::RenderViewContext primaryView{};
        primaryView.screenReadability =
            SCENE_CAPTURE::CollectScreenReadability(state_->runtime.world, state_->runtime.sceneCaptureService);
        primaryView.viewId = RENDER3D::kPrimaryRenderViewId;
        primaryView.purpose =
            state_->runtime.playActive ? RENDER3D::RenderViewPurpose::Game : RENDER3D::RenderViewPurpose::EditorScene;
        primaryView.cameraFrame = &state_->camera.resolvedFrame;
        const GameProjectPrimaryViewSource presentationSource =
            state_->runtime.playActive                     ? GameProjectPrimaryViewSource::Game
            : IsEditorCameraPreviewActive()                ? GameProjectPrimaryViewSource::EditorCameraPreview
            : state_->camera.sequencePreviewSnapshotActive ? GameProjectPrimaryViewSource::EditorSequencePreview
                                                           : GameProjectPrimaryViewSource::EditorScene;
        const uint64_t sourceCameraObjectId = state_->camera.resolvedFrame.sourceCameraObjectId;
        uint64_t sourceCameraObjectHandle = 0u;
        bool sourceCameraIdentityResolved = sourceCameraObjectId == 0u;
        if (sourceCameraObjectId != 0u) {
            const GameObject* sourceCameraObject =
                state_->runtime.world.FindObject(SceneObjectId{sourceCameraObjectId});
            if (sourceCameraObject != nullptr) {
                sourceCameraObjectHandle = sourceCameraObject->GetRuntimeHandle().ToValue();
                sourceCameraIdentityResolved = sourceCameraObjectHandle != 0u;
            }
        }
        if (sourceCameraIdentityResolved) {
            SERVICES::detail::SubmitPrimaryViewCameraFrame(
                state_->camera.resolvedFrame, sourceCameraObjectHandle, presentationSource,
                state_->runtime.world.GetRuntimeIdentity(), frame.frameIndex);
        }
#if defined(HIKARI_WITH_EDITOR)
        RENDER3D::EDITORVIEW::SetPrimaryViewCaptureFrame(state_->camera.resolvedFrame,
                                                         state_->identity.documentRevision);
#endif
        primaryView.assetRegistry = &state_->assets.Registry();
        primaryView.worldSpriteFrameSource = &RenderSubmissionSystem::GetWorldSpriteFrameSource();
        primaryView.localFogVolumes = localFogVolumes;
        primaryView.worldTextFrameSource = &RenderSubmissionSystem::GetWorldTextFrameSource();
        primaryView.projectRenderExtensions = {
            &state_->runtime.systemScheduler,
            [](void* context, const GameProjectRenderFrameView& frame) noexcept {
                return static_cast<SystemScheduler*>(context)->ObserveRenderFrame(frame);
            },
            [](void* context, uint32_t& outCount) noexcept {
                return static_cast<SystemScheduler*>(context)->GetRenderPassCount(outCount);
            },
            [](void* context, uint32_t index, GameProjectRenderPassDesc& outDescriptor) noexcept {
                return static_cast<SystemScheduler*>(context)->DescribeRenderPass(index, outDescriptor);
            },
            [](void* context, const char* stableId, const GameProjectRenderPassContext& pass,
               bool& outCallbackEntered) noexcept {
                return static_cast<SystemScheduler*>(context)->ExecuteRenderPass(stableId, pass, &outCallbackEntered);
            },
            state_->runtime.world.GetRuntimeIdentity()};
        VFX::VfxPlaybackService* playback = state_->runtime.world.Services().Find<VFX::VfxPlaybackService>();
        RENDER3D::PARTICLES::ParticleRenderFrameSource particleFrameSource{};
        if (playback != nullptr) {
            particleFrameSource = {&playback->GetRenderInput(), &state_->assets.Registry(),
                                   state_->runtime.materialExternalValueSnapshot.get()};
            primaryView.particleFrameSource = &particleFrameSource;
        }
        if (auto* combat = state_->runtime.world.Services().Find<VFX::CombatPresentationService>()) {
            primaryView.combatPresentationFrameSource = &combat->GetFrameSource();
        }
        if (auto* reactions = state_->runtime.world.Services().Find<VFX::ActionPostReactionService>()) {
            primaryView.actionPostReactionFrameSource = &reactions->GetFrameSource();
        }
        SCENE_CAPTURE::RenderSecondaryCapture(*state_, graphics, activeEnvironment, sceneLighting, frame,
                                              primaryView.particleFrameSource, primaryView.worldSpriteFrameSource,
                                              primaryView.worldTextFrameSource);
        const MESHRENDERER::MeshRendererFrameResult modelFrame = MODELRENDERER::RenderAll(
            graphics, primaryView, activeEnvironment, sceneLighting, state_->editor.viewportDebugViewState.renderView);
        if (modelFrame.CanAcknowledgeMassRenderPublication()) {
            RenderSubmissionSystem::AcknowledgeMassRenderFrame(state_->runtime.world, frame);
        }
        const auto primaryParticleReceipt = MESHRENDERER::GetParticleGpuDrivenPassStats();
        SubmitFrozenCullingCameraDebugFrustum();
        if (playback != nullptr && primaryParticleReceipt.simulationAccepted &&
            primaryParticleReceipt.simulationAcceptedToken == playback->GetRenderInput().GetDeliveryToken()) {
            (void)playback->AcknowledgeRenderInput(primaryParticleReceipt.simulationAcceptedToken);
        }
        RENDERER3D::DEBUG::RenderAll(renderCamera, static_cast<float>(captureW), static_cast<float>(captureH));
    }
#if defined(HIKARI_WITH_EDITOR)
    (void)RENDER3D::EDITORVIEW::RenderPending(
        activeEnvironment, sceneLighting, state_->identity.documentRevision, state_->assets.Models(),
        state_->lighting.sky, &state_->assets.Registry(), state_->runtime.materialExternalValueSnapshot.get(),
        &RenderSubmissionSystem::GetWorldSpriteFrameSource(), &RenderSubmissionSystem::GetWorldTextFrameSource());
#endif
}
const std::string& DocumentSceneBase::GetSceneId() const { return state_->identity.sceneId; }
const std::string& DocumentSceneBase::GetScenePath() const { return state_->identity.scenePath; }
const std::string& DocumentSceneBase::GetSceneLoadError() const noexcept { return state_->identity.loadError; }
uint64_t DocumentSceneBase::GetSceneDocumentRevision() const noexcept { return state_->identity.documentRevision; }

World& DocumentSceneBase::GetWorld() { return state_->runtime.world; }
const World& DocumentSceneBase::GetWorld() const { return state_->runtime.world; }
SceneDocument& DocumentSceneBase::GetSceneDocument() { return state_->identity.document; }
const SceneDocument& DocumentSceneBase::GetSceneDocument() const { return state_->identity.document; }
SceneEnvironment& DocumentSceneBase::GetSceneEnvironment() { return state_->lighting.environment; }
const SceneEnvironment& DocumentSceneBase::GetSceneEnvironment() const { return state_->lighting.environment; }
AssetRegistry& DocumentSceneBase::GetAssetRegistry() { return state_->assets.Registry(); }

const AssetRegistry& DocumentSceneBase::GetAssetRegistry() const { return state_->assets.Registry(); }
BINDING::RuntimeBindingStore& DocumentSceneBase::GetRuntimeBindingStore() noexcept {
    return state_->runtime.bindingStore;
}
const BINDING::RuntimeBindingStore& DocumentSceneBase::GetRuntimeBindingStore() const noexcept {
    return state_->runtime.bindingStore;
}
bool DocumentSceneBase::ReloadRuntimeBindingCatalog(std::string& outMessage) {
    BINDING::Catalog catalog{};
    const std::filesystem::path path = BINDING::GetCatalogPath(state_->assets.Database().GetProjectRoot());
    if (!std::filesystem::exists(path)) {
        catalog = {};
    } else if (!BINDING::LoadCatalog(path, catalog, outMessage)) {
        return false;
    }
    if (!state_->runtime.bindingStore.ReloadCatalog(std::move(catalog))) {
        outMessage = "runtime binding catalog validation failed";
        return false;
    }
    outMessage.clear();
    return true;
}
AssetDatabase& DocumentSceneBase::GetAssetDatabase() { return state_->assets.Database(); }
const AssetDatabase& DocumentSceneBase::GetAssetDatabase() const { return state_->assets.Database(); }
ModelManager& DocumentSceneBase::GetModelManager() { return state_->assets.Models(); }
SkyManager& DocumentSceneBase::GetSkyManager() { return state_->lighting.sky; }
ComponentRegistry& DocumentSceneBase::GetComponentRegistry() { return state_->runtime.componentRegistry; }
const ComponentSystemPolicy& DocumentSceneBase::GetComponentSystemPolicy() const noexcept {
    return state_->runtime.componentSystemPolicy;
}
const SystemTypeRegistry& DocumentSceneBase::GetSystemTypeRegistry() const noexcept {
    return state_->runtime.systemTypeRegistry;
}
const SystemScheduler& DocumentSceneBase::GetSystemScheduler() const noexcept {
    return state_->runtime.systemScheduler;
}
const RuntimeFeatureCatalog& DocumentSceneBase::GetRuntimeFeatureCatalog() const noexcept {
    return state_->runtime.featureCatalog;
}
bool DocumentSceneBase::AddBorrowedRuntimeExtension(IRuntimeExtension& extension) {
    if (state_->runtime.initialized) {
        HIKARI_LOG_WARN("[RuntimeExtension] extensions must be added before OnEnter");
        return false;
    }
    return state_->runtime.extensionHost.AddBorrowed(extension);
}
bool DocumentSceneBase::IsRuntimeFeatureActive(std::string_view featureId) const noexcept {
    return state_->runtime.featureCatalog.IsFeatureActive(featureId);
}
std::vector<SceneSystemData> DocumentSceneBase::CreateProjectDefaultSceneSystems() const {
    return state_->runtime.featureCatalog.CreateDefaultSceneSystems();
}
bool DocumentSceneBase::ReloadRuntimeFeaturesFromProjectSettings() {
    if (state_->runtime.playActive) {
        HIKARI_LOG_WARN("[RuntimeFeature] feature settings cannot reload during Play");
        return false;
    }
    const bool registered = RegisterRuntimeFeatures();
    const bool rebuilt = RebuildRuntimeWorld(true);
    return registered && rebuilt;
}

SceneRuntimeBuilder& DocumentSceneBase::GetRuntimeBuilder() { return state_->runtime.builder; }

const SceneRuntimeBuilder& DocumentSceneBase::GetRuntimeBuilder() const { return state_->runtime.builder; }
Camera3D& DocumentSceneBase::GetCamera() { return state_->camera.editorCamera; }
const Camera3D& DocumentSceneBase::GetCamera() const { return state_->camera.editorCamera; }
CameraDirector& DocumentSceneBase::GetCameraDirector() { return state_->camera.director; }
const CameraDirector& DocumentSceneBase::GetCameraDirector() const { return state_->camera.director; }
const RENDER3D::ResolvedCameraFrame& DocumentSceneBase::GetResolvedCameraFrame() const {
    return state_->camera.resolvedFrame;
}
DebugCameraController3D& DocumentSceneBase::GetDebugCamera() { return state_->camera.debugCamera; }
bool& DocumentSceneBase::GetEnvironmentLightingEnabled() { return state_->lighting.environmentLightingEnabled; }
ComponentGizmoRegistry& DocumentSceneBase::GetComponentGizmoRegistry() noexcept {
    return state_->editor.componentGizmoRenderer.Registry();
}
const ComponentGizmoRegistry& DocumentSceneBase::GetComponentGizmoRegistry() const noexcept {
    return state_->editor.componentGizmoRenderer.Registry();
}
void DocumentSceneBase::SetComponentGizmoState(const ComponentGizmoState& state) {
    state_->editor.componentGizmoState = state;
}
void DocumentSceneBase::SetViewportOverlayState(const ViewportOverlayState& state) {
    state_->editor.viewportOverlayState = state;
}
void DocumentSceneBase::SetViewportPerformanceState(const ViewportPerformanceState& state) {
    state_->editor.viewportPerformanceState = state;
}
void DocumentSceneBase::SetViewportDebugViewState(const ViewportDebugViewState& state) {
    state_->editor.viewportDebugViewState = state;
}
void DocumentSceneBase::SetSelectedGizmoObjectId(SceneObjectId id) { state_->editor.selectedGizmoObjectId = id; }
#if defined(HIKARI_WITH_EDITOR)
bool DocumentSceneBase::RequestLightingBake(bool force, SceneObjectId selectedObjectId) {
    return state_->baking.RequestLightingBake(*this, force, selectedObjectId);
}
bool DocumentSceneBase::ClearLightingBake() { return state_->baking.ClearLightingBake(*this); }
TOOLS::BAKING::LightingBakeJobState DocumentSceneBase::GetLightingBakeJobState() const {
    return state_->baking.GetJobState();
}
bool DocumentSceneBase::HasLastLightingBakeReport() const { return state_->baking.HasLastReport(); }
const TOOLS::BAKING::LightingBakeReport& DocumentSceneBase::GetLastLightingBakeReport() const {
    return state_->baking.GetLastReport();
}
#endif
bool DocumentSceneBase::UseDebugCamera() const { return !state_->runtime.playActive; }

bool DocumentSceneBase::DrawDebugHelpers() const {
    return !state_->runtime.playActive && !state_->camera.sequencePreview.IsValid() && SERVICES::IsEditorUIEnabled();
}
bool DocumentSceneBase::UseEnvironmentLighting() const { return state_->lighting.environmentLightingEnabled; }

} // namespace HIKARI
