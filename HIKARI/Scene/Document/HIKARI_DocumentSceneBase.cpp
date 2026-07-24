#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Document/Internal/HIKARI_DocumentSceneState.h"

#include <array>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <numbers>
#include <utility>

#include "HIKARI_3D.h"
#include "HIKARI_Services.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Core/Presentation/HIKARI_LogicalDisplayDefaults.h"
#include "Core/HIKARI_TimeService.h"
#include "Render3D/HIKARI_LightDebugDraw.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
#if defined(HIKARI_WITH_EDITOR)
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#endif
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Scene/Sequencer/Drivers/HIKARI_CameraSequenceTrackDriver.h"
#include "Vfx/Runtime/HIKARI_VfxAsset.h"
#include "Vfx/Runtime/HIKARI_VfxSystem.h"
#include "Vfx/Post/HIKARI_PostSystem.h"
#include "Vfx/Post/HIKARI_PostProfile.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"

namespace HIKARI {
    namespace {
        constexpr const char* kDefaultGlobalPostProfileId = "Ani";

        bool TryUnprojectClipCorner(
            const MATH::Mat4& invViewProj,
            float x,
            float y,
            float z,
            MATH::Vec3& out) {

            const MATH::Vec4 clip{ x, y, z, 1.0f };
            const MATH::Vec4 world = invViewProj.TransformPoint(clip);
            if (std::abs(world.w) < 1.0e-6f ||
                !MATH::IsFinite(world)) {
                return false;
            }

            const float invW = 1.0f / world.w;
            out = { world.x * invW, world.y * invW, world.z * invW };
            return true;
        }

        void SubmitFrozenCullingCameraDebugFrustum() {
            if (!MESHRENDERER::IsGpuDrivenCullingCameraFrozen()) {
                return;
            }

            const MESHRENDERER::CameraCB* cullingCamera =
                MESHRENDERER::GetGpuDrivenCullingCameraConstants();
            if (cullingCamera == nullptr) {
                return;
            }

            constexpr std::array<std::array<float, 3>, 8> kClipCorners{ {
                { -1.0f, -1.0f, 0.0f },
                {  1.0f, -1.0f, 0.0f },
                {  1.0f,  1.0f, 0.0f },
                { -1.0f,  1.0f, 0.0f },
                { -1.0f, -1.0f, 1.0f },
                {  1.0f, -1.0f, 1.0f },
                {  1.0f,  1.0f, 1.0f },
                { -1.0f,  1.0f, 1.0f },
            } };

            std::array<MATH::Vec3, 8> corners{};
            for (size_t i = 0; i < kClipCorners.size(); ++i) {
                const auto& c = kClipCorners[i];
                if (!TryUnprojectClipCorner(
                    cullingCamera->invViewProj,
                    c[0],
                    c[1],
                    c[2],
                    corners[i])) {
                    return;
                }
            }

            constexpr std::array<std::array<int, 2>, 12> kEdges{ {
                { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
                { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
                { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
            } };
            constexpr unsigned int kNearColor = 0xFFD166FFu;
            constexpr unsigned int kFrustumColor = 0x37C8FFFFu;
            constexpr unsigned int kRayColor = 0x8F6AFFFFu;
            for (size_t i = 0; i < kEdges.size(); ++i) {
                const auto& edge = kEdges[i];
                RENDERER3D::DEBUG::SubmitLine3D({
                    corners[static_cast<size_t>(edge[0])],
                    corners[static_cast<size_t>(edge[1])],
                    i < 4 ? kNearColor : kFrustumColor,
                    RENDERER3D::DEBUG::DebugDepthMode::XRay
                });
            }

            const MATH::Vec3 cullingPosition{
                cullingCamera->cameraPos.x,
                cullingCamera->cameraPos.y,
                cullingCamera->cameraPos.z
            };
            for (size_t i = 0; i < 4; ++i) {
                RENDERER3D::DEBUG::SubmitLine3D({
                    cullingPosition,
                    corners[i],
                    kRayColor,
                    RENDERER3D::DEBUG::DebugDepthMode::XRay
                });
            }
        }
    }

    DocumentSceneBase::DocumentSceneBase(std::string sceneId)
        : state_(std::make_unique<DocumentSceneState>()) {

        state_->identity.sceneId = std::move(sceneId);

        (void)state_->camera.sequencePlayback.RegisterDriver(
            std::make_unique<SEQUENCER::CameraSequenceTrackDriver>(
                state_->camera.director,
                state_->runtime.world,
                state_->camera.editorCamera));
    }

    DocumentSceneBase::~DocumentSceneBase() = default;

    void DocumentSceneBase::OnEnter() {
        state_->runtime.initialized = true;
        state_->camera.editorCamera.SetPerspective(
            60.0f * std::numbers::pi_v<float> / 180.0f,
            static_cast<float>(
                PRESENTATION::kDefaultLogicalWidth) /
                static_cast<float>(
                    PRESENTATION::kDefaultLogicalHeight),
            0.1f,
            100.0f);
        state_->camera.gameplayCamera = state_->camera.editorCamera;
        state_->camera.debugCamera.Reset({ 0.0f, 2.0f, -6.0f }, 0.0f, 0.0f);
        state_->runtime.fixedStepClock.SetSettings(FixedStepSettings{
            TIME::GetFixedDeltaSeconds(),
            8,
            0.25f
        });
        state_->runtime.fixedStepClock.Reset();
        if (!ConfigureRuntimeWorldServices()) {
            HIKARI_LOG_ERROR(
                "[RuntimeExtension] world service registration failed");
        }

        ReloadAssets();
        if (!RegisterRuntimeFeatures()) {
            HIKARI_LOG_ERROR(
                "[RuntimeFeature] one or more built-in features failed to install");
        }
        state_->assets.SequenceAssets().SetAssetDatabase(&state_->assets.Database());
        state_->camera.sequencePlayback.SetAssetStore(&state_->assets.SequenceAssets());
        state_->runtime.animationStateMachineAssets.SetAssetDatabase(&state_->assets.Database());
        VFX::SetAssetRegistry(&state_->assets.Registry());
        if (!OpenStartupSceneAsset()) {
            CreateTransientEmptySceneDocument();
            RebuildRuntimeWorld();
        }
    }
    void DocumentSceneBase::OnExit() {
        state_->camera.sequencePlayback.Reset();
        state_->camera.currentCameraSequenceHandle = {};
        state_->camera.rigService.Clear();
        state_->assets.SequenceAssets().Clear();
        state_->runtime.animationStateMachineAssets.Clear();
        state_->runtime.animationStateMachineRuntimeService.Clear();
        state_->runtime.systemScheduler.DetachWorld(state_->runtime.world);
        state_->runtime.systemScheduler.Clear();
        state_->runtime.world.Services().Clear();
        state_->runtime.fixedStepClock.Reset();
        state_->runtime.initialized = false;
        RuntimeSceneContext::SetCurrentWorld(nullptr);
    }
    void DocumentSceneBase::Update(float dt) {
        FrameContext frame = HIKARI::TIME::GetFrameContext();
        if (std::abs(
                state_->runtime.fixedStepClock.GetSettings().stepSeconds -
                frame.fixedDt) > 1e-6f) {
            FixedStepSettings settings =
                state_->runtime.fixedStepClock.GetSettings();
            settings.stepSeconds = frame.fixedDt;
            state_->runtime.fixedStepClock.SetSettings(settings);
        }
        state_->runtime.world.BeginFrame(frame.frameIndex);
        state_->camera.rigService.BeginFrame(frame.frameIndex);
        if (state_->runtime.playActive && state_->camera.runtimePreviewCameraActive) {
            state_->camera.runtimePreviewCamera.Update(
                dt,
                state_->camera.gameplayCamera,
                SERVICES::GetInputSnapshot(),
                CameraControlInputContext::RuntimeWindow);
        } else if (UseDebugCamera() && !IsEditorCameraPreviewActive()) {
            state_->camera.debugCamera.Update(
                dt,
                state_->camera.editorCamera,
                SERVICES::GetInputSnapshot(),
                CameraControlInputContext::EditorViewport);
        }
        state_->runtime.systemScheduler.PreUpdate(state_->runtime.world, frame);
        state_->runtime.world.Update(dt);

        const FixedStepFramePlan fixedPlan =
            state_->runtime.fixedStepClock.Advance(frame.gameDt);
        frame.fixedDt = fixedPlan.stepSeconds;
        frame.fixedStepsThisFrame = fixedPlan.stepCount;
        frame.fixedInterpolationAlpha =
            fixedPlan.interpolationAlpha;
        frame.droppedFixedTime = fixedPlan.droppedSeconds;
        TIME::ReportFixedStepFrame(
            state_->runtime.fixedStepClock.GetCompletedTickCount(),
            fixedPlan.stepCount,
            fixedPlan.interpolationAlpha,
            fixedPlan.droppedSeconds);
        for (uint32_t stepIndex = 0;
            stepIndex < fixedPlan.stepCount;
            ++stepIndex) {

            FrameContext fixedFrame = frame;
            fixedFrame.rawDt = fixedPlan.stepSeconds;
            fixedFrame.gameDt = fixedPlan.stepSeconds;
            fixedFrame.unscaledDt = fixedPlan.stepSeconds;
            fixedFrame.fixedTickIndex =
                fixedPlan.GetTickIndex(stepIndex);
            fixedFrame.fixedStepIndex = stepIndex;
            fixedFrame.isFixedStep = true;
            state_->runtime.world.BeginFixedStep(fixedFrame.fixedTickIndex);
            state_->runtime.systemScheduler.PreFixedUpdate(state_->runtime.world, fixedFrame);
            state_->runtime.systemScheduler.FixedUpdate(state_->runtime.world, fixedFrame);
            state_->runtime.systemScheduler.PostFixedUpdate(state_->runtime.world, fixedFrame);
        }

        state_->runtime.systemScheduler.Update(state_->runtime.world, frame);
        state_->runtime.systemScheduler.LateUpdate(state_->runtime.world, frame);

        if (state_->runtime.playActive) {
            if (IsRuntimeFeatureActive(
                    RuntimeFeatureIds::Cinematics)) {
                state_->camera.sequencePlayback.Update(dt);
            }
            state_->camera.resolvedFrame = state_->camera.director.Resolve(
                state_->runtime.world,
                state_->camera.gameplayCamera,
                state_->camera.editorCamera.GetAspect(),
                dt);
            state_->camera.editorCamera = state_->camera.resolvedFrame.camera;
            state_->camera.gameplayCamera = state_->camera.director.GetControlCamera();
        } else if (IsEditorCameraPreviewActive()) {
            state_->camera.resolvedFrame = state_->camera.director.Resolve(
                state_->runtime.world,
                state_->camera.editorCameraPreviewSnapshot,
                state_->camera.editorCamera.GetAspect(),
                dt);
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
        int captureW = 0;
        int captureH = 0;
        POST::PostSystem::GetSceneCaptureSize(captureW, captureH);
        if (captureW > 0 && captureH > 0) {
            const float aspect = static_cast<float>(captureW) / static_cast<float>(captureH);
            const bool projectionChanged = std::abs(state_->camera.editorCamera.GetAspect() - aspect) > 1.0e-5f;
            state_->camera.editorCamera.SetPerspective(
                state_->camera.editorCamera.GetFovYRad(),
                aspect,
                state_->camera.editorCamera.GetNearZ(),
                state_->camera.editorCamera.GetFarZ());
            state_->camera.resolvedFrame.camera = state_->camera.editorCamera;
            state_->camera.resolvedFrame.projectionChanged =
                state_->camera.resolvedFrame.projectionChanged || projectionChanged;
            state_->camera.resolvedFrame.valid = true;
        }

        if (state_->baking.ProcessReflectionProbeBakeJob(*this)) {
            return;
        }
        if (state_->baking.ProcessLightProbeBakeJob(*this)) {
            return;
        }

        RENDERER3D::Reset();
        MODELRENDERER::ResetFrame();
        SKYRENDERER::Reset();

        if (state_->lighting.environment.post.enabled && state_->lighting.environment.post.globalPostProfileId.empty()) {
            state_->lighting.environment.post.globalPostProfileId = kDefaultGlobalPostProfileId;
            state_->lighting.environment.post.valuesInitialized = false;
        }

        if (state_->lighting.environment.post.enabled && !state_->lighting.environment.post.globalPostProfileId.empty()) {
            if (!state_->lighting.environment.post.valuesInitialized) {
                PostProfile profile{};
                if (PostProfile::LoadById(state_->lighting.environment.post.globalPostProfileId, profile)) {
                    profile.CopyValuesTo(state_->lighting.environment.post.paramValues);
                    state_->lighting.environment.post.valuesInitialized = true;
                }
            }
            POST::PostSystem::SetGlobalProfile(state_->lighting.environment.post.globalPostProfileId, state_->lighting.environment.post.paramValues);
        } else {
            POST::PostSystem::ClearGlobalProfile();
        }
        POST::PostSystem::SetBloomSettings(state_->lighting.environment.bloom);
        POST::PostSystem::SetToneMappingSettings(state_->lighting.environment.toneMapping);

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
        RenderSubmissionSystem::SetAssetContext(&state_->assets.Registry(), state_->assets.Database().GetProjectRoot());
        state_->runtime.systemScheduler.PreRender(state_->runtime.world, frame);
        state_->runtime.systemScheduler.Render(state_->runtime.world, frame);
        state_->runtime.systemScheduler.PostRender(state_->runtime.world, frame);
        SceneEnvironment activeEnvironment = state_->lighting.environment;
        activeEnvironment.directional.direction = MATH::Normalize(activeEnvironment.directional.direction);
        const bool editorSsaoSuppressed =
            DrawDebugHelpers() &&
            state_->editor.viewportPerformanceState.disableSsaoInEditorViewport;
        activeEnvironment.ambientOcclusion.editorViewportSuppressed = editorSsaoSuppressed;
        if (!UseEnvironmentLighting()) {
            activeEnvironment.directional.intensity = 0.0f;
            activeEnvironment.ambient.intensity = 0.0f;
            activeEnvironment.specularIntensity = 0.0f;
            for (PointLight& pointLight : activeEnvironment.pointLights) {
                pointLight.intensity = 0.0f;
            }
        }

        MESHRENDERER::SetGpuDrivenCullingCameraFreezeEnabled(
            DrawDebugHelpers() && state_->editor.viewportDebugViewState.freezeCullingCamera);
        const Camera3D& renderCamera = state_->camera.editorCamera;

        SKYRENDERER::Render(renderCamera, activeEnvironment, state_->assets.Models(), state_->lighting.sky);
        if (DrawDebugHelpers()) {
            if (state_->editor.viewportOverlayState.showLights) {
                LIGHTDEBUGDRAW::SubmitDirectionalLightArrow(activeEnvironment.directional.direction, activeEnvironment);
                LIGHTDEBUGDRAW::SubmitPointLightDebug(activeEnvironment);
            }
#if defined(HIKARI_WITH_EDITOR)
            if (state_->editor.viewportOverlayState.showReflectionProbe) {
                state_->editor.reflectionProbeGizmoRenderer.Submit(activeEnvironment, state_->editor.viewportOverlayState, renderCamera);
            }
            state_->editor.lightProbeVolumeGizmoRenderer.Submit(
                state_->identity.document.lightingBake.lightProbeVolume,
                state_->editor.viewportOverlayState);
#endif
        }

        if (DrawDebugHelpers()) {
            state_->editor.componentGizmoRenderer.SubmitWorldGizmos(
                state_->runtime.world,
                state_->editor.componentGizmoState,
                state_->editor.selectedGizmoObjectId,
                renderCamera.GetAspect());
        }
        RENDER3D::RenderViewContext primaryView{};
        primaryView.viewId = RENDER3D::kPrimaryRenderViewId;
        primaryView.purpose = state_->runtime.playActive
            ? RENDER3D::RenderViewPurpose::Game
            : RENDER3D::RenderViewPurpose::EditorScene;
        primaryView.cameraFrame = &state_->camera.resolvedFrame;
        MODELRENDERER::RenderAll(primaryView, activeEnvironment, state_->editor.viewportDebugViewState.renderView);
        SubmitFrozenCullingCameraDebugFrustum();
        RENDERER3D::RenderAll(renderCamera, static_cast<float>(captureW), static_cast<float>(captureH));
        VFX::Render(renderCamera);
#if defined(HIKARI_WITH_EDITOR)
        (void)RENDER3D::EDITORVIEW::RenderPending(
            activeEnvironment,
            state_->identity.documentRevision);
#endif
    }
    void DocumentSceneBase::RenderImGui() {
        if (!SERVICES::IsEditorUIEnabled() && !SERVICES::ArePortableObjectToolsEnabled()) {
            state_->runtime.world.RenderImGui();
        }
    }
    const std::string& DocumentSceneBase::GetSceneId() const {
        return state_->identity.sceneId;
    }
    const std::string& DocumentSceneBase::GetScenePath() const {
        return state_->identity.scenePath;
    }
    uint64_t DocumentSceneBase::GetSceneDocumentRevision() const noexcept {
        return state_->identity.documentRevision;
    }

    void DocumentSceneBase::SetSceneId(std::string sceneId) {
        state_->identity.sceneId = std::move(sceneId);
    }
    void DocumentSceneBase::SetScenePath(std::string scenePath) {
        state_->identity.scenePath = std::move(scenePath);
    }
    World& DocumentSceneBase::GetWorld() {
        return state_->runtime.world;
    }
    const World& DocumentSceneBase::GetWorld() const {
        return state_->runtime.world;
    }
    SceneDocument& DocumentSceneBase::GetSceneDocument() {
        return state_->identity.document;
    }
    const SceneDocument& DocumentSceneBase::GetSceneDocument() const {
        return state_->identity.document;
    }
    SceneEnvironment& DocumentSceneBase::GetSceneEnvironment() {
        return state_->lighting.environment;
    }
    const SceneEnvironment& DocumentSceneBase::GetSceneEnvironment() const {
        return state_->lighting.environment;
    }
    AssetRegistry& DocumentSceneBase::GetAssetRegistry() {
        return state_->assets.Registry();
    }

    const AssetRegistry& DocumentSceneBase::GetAssetRegistry() const {
        return state_->assets.Registry();
    }
    AssetDatabase& DocumentSceneBase::GetAssetDatabase() {
        return state_->assets.Database();
    }
    const AssetDatabase& DocumentSceneBase::GetAssetDatabase() const {
        return state_->assets.Database();
    }
    ModelManager& DocumentSceneBase::GetModelManager() {
        return state_->assets.Models();
    }
    SkyManager& DocumentSceneBase::GetSkyManager() {
        return state_->lighting.sky;
    }
    ComponentRegistry& DocumentSceneBase::GetComponentRegistry() {
        return state_->runtime.componentRegistry;
    }
    const ComponentSystemPolicy&
        DocumentSceneBase::GetComponentSystemPolicy() const noexcept {
        return state_->runtime.componentSystemPolicy;
    }
    const SystemTypeRegistry&
        DocumentSceneBase::GetSystemTypeRegistry() const noexcept {
        return state_->runtime.systemTypeRegistry;
    }
    const SystemScheduler&
        DocumentSceneBase::GetSystemScheduler() const noexcept {
        return state_->runtime.systemScheduler;
    }
    const RuntimeFeatureCatalog&
        DocumentSceneBase::GetRuntimeFeatureCatalog() const noexcept {
        return state_->runtime.featureCatalog;
    }
    const RuntimeFeatureInstallReport&
        DocumentSceneBase::GetRuntimeFeatureInstallReport() const noexcept {
        return state_->runtime.featureInstallReport;
    }
    bool DocumentSceneBase::AddRuntimeExtension(
        std::unique_ptr<IRuntimeExtension> extension) {

        if (state_->runtime.initialized) {
            HIKARI_LOG_WARN(
                "[RuntimeExtension] extensions must be added before OnEnter");
            return false;
        }
        return state_->runtime.extensionHost.Add(std::move(extension));
    }
    std::vector<std::string>
        DocumentSceneBase::GetRuntimeExtensionIds() const {
        return state_->runtime.extensionHost.GetExtensionIds();
    }
    bool DocumentSceneBase::IsRuntimeFeatureActive(
        std::string_view featureId) const noexcept {
        return state_->runtime.featureCatalog.IsFeatureActive(featureId);
    }
    std::vector<SceneSystemData>
        DocumentSceneBase::CreateProjectDefaultSceneSystems() const {
        return state_->runtime.featureCatalog.CreateDefaultSceneSystems();
    }
    bool DocumentSceneBase::ReloadRuntimeFeaturesFromProjectSettings() {
        if (state_->runtime.playActive) {
            HIKARI_LOG_WARN(
                "[RuntimeFeature] feature settings cannot reload during Play");
            return false;
        }
        const bool registered = RegisterRuntimeFeatures();
        const bool rebuilt = RebuildRuntimeWorld();
        return registered && rebuilt;
    }

    SceneRuntimeBuilder& DocumentSceneBase::GetRuntimeBuilder() {
        return state_->runtime.builder;
    }

    const SceneRuntimeBuilder& DocumentSceneBase::GetRuntimeBuilder() const {
        return state_->runtime.builder;
    }
    Camera3D& DocumentSceneBase::GetCamera() {
        return state_->camera.editorCamera;
    }
    const Camera3D& DocumentSceneBase::GetCamera() const {
        return state_->camera.editorCamera;
    }
    CameraDirector& DocumentSceneBase::GetCameraDirector() {
        return state_->camera.director;
    }
    const CameraDirector& DocumentSceneBase::GetCameraDirector() const {
        return state_->camera.director;
    }
    const RENDER3D::ResolvedCameraFrame& DocumentSceneBase::GetResolvedCameraFrame() const {
        return state_->camera.resolvedFrame;
    }
    DebugCameraController3D& DocumentSceneBase::GetDebugCamera() {
        return state_->camera.debugCamera;
    }
    bool& DocumentSceneBase::GetEnvironmentLightingEnabled() {
        return state_->lighting.environmentLightingEnabled;
    }
    ComponentGizmoRegistry&
        DocumentSceneBase::GetComponentGizmoRegistry() noexcept {
        return state_->editor.componentGizmoRenderer.Registry();
    }
    const ComponentGizmoRegistry&
        DocumentSceneBase::GetComponentGizmoRegistry() const noexcept {
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
    void DocumentSceneBase::SetSelectedGizmoObjectId(SceneObjectId id) {
        state_->editor.selectedGizmoObjectId = id;
    }
    bool DocumentSceneBase::UseDebugCamera() const {
        return !state_->runtime.playActive;
    }

    bool DocumentSceneBase::DrawDebugHelpers() const {
        return !state_->runtime.playActive && SERVICES::IsEditorUIEnabled();
    }
    bool DocumentSceneBase::UseEnvironmentLighting() const {
        return state_->lighting.environmentLightingEnabled;
    }


} // namespace HIKARI
