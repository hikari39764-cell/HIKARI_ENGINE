#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"

#if defined(HIKARI_WITH_EDITOR)

#include <algorithm>
#include <array>
#include <memory>
#include <unordered_map>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/Color/HIKARI_ExposureState.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/Lighting/HIKARI_DefaultSkyEnvironment.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Views/HIKARI_EditorInteractiveStylizedView.h"
#include "Vfx/Post/HIKARI_PostCommon.h"
#include "Vfx/Post/HIKARI_PostEffect.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI::RENDER3D::EDITORVIEW {

    namespace {

        constexpr uint32_t kMaxInteractiveViewExtent = 4096u;
        constexpr DXGI_FORMAT kInteractiveViewFormat =
            DXGI_FORMAT_R16G16B16A16_FLOAT;
        constexpr DXGI_FORMAT kInteractiveDisplayFormat =
            DXGI_FORMAT_R8G8B8A8_UNORM;

        struct InteractiveViewState {
            EditorInteractiveViewRequest request{};
            EditorInteractiveViewOutput output{};
            std::unique_ptr<RenderTarget2D> target{};
            std::unique_ptr<RenderTarget2D> displayTarget{};
            std::unique_ptr<RenderTarget2D> antialiasedTarget{};
            EditorInteractiveStylizedView stylizedView{};
            std::array<
                RenderResourceView,
                GFX::kFrameResourceCount> outputSrvs{};
            std::array<uint64_t, GFX::kFrameResourceCount>
                outputSrvGenerations{};
            std::array<ID3D12Resource*, GFX::kFrameResourceCount>
                outputSrvResources{};
            uint64_t targetGeneration = 0;
            uint64_t lastRenderedSerial = 0;
            bool hasRequest = false;
        };

        std::unordered_map<uint64_t, std::unique_ptr<InteractiveViewState>>
            g_views{};
        std::unique_ptr<POST::QuadDrawer> g_displayQuad{};
        std::unique_ptr<POST::PostEffect> g_displayEffect{};
        std::unique_ptr<POST::PostEffect> g_fxaaEffect{};
        uint64_t g_scheduleSerial = 0u;
        EditorInteractiveSchedulerStats g_schedulerStats{};

        InteractiveViewState& FindOrCreateState(RenderViewId viewId) {
            auto found = g_views.find(viewId.value);
            if (found != g_views.end()) {
                return *found->second;
            }
            auto state = std::make_unique<InteractiveViewState>();
            state->output.viewId = viewId;
            InteractiveViewState& result = *state;
            g_views.emplace(viewId.value, std::move(state));
            return result;
        }

        bool IsRenderableRequest(
            const EditorInteractiveViewRequest& request,
            uint64_t currentSceneRevision) {

            return request.viewId.value != 0 &&
                request.visible &&
                request.width != 0 &&
                request.height != 0 &&
                (request.sceneSourceOverride != nullptr ||
                    request.sceneRevision == currentSceneRevision) &&
                request.cameraFrame.valid;
        }

        uint32_t ClampExtent(uint32_t value) {
            return (std::min)(value, kMaxInteractiveViewExtent);
        }


        bool EnsureDisplayPipeline() {
            POST::PostEffect::UpdateContext(GFX::GetCurrentContext());
            if (g_displayQuad == nullptr) {
                auto quad = std::make_unique<POST::QuadDrawer>();
                if (!quad->Init(GFX::GetCurrentContext())) {
                    return false;
                }
                g_displayQuad = std::move(quad);
            } else {
                g_displayQuad->UpdateContext(GFX::GetCurrentContext());
            }

            if (g_displayEffect == nullptr) {
                auto effect = std::make_unique<POST::PostEffect>();
                if (!effect->LoadPixelShader(
                        L"HIKARI/Shaders/Post_ToneMappingPS.hlsl")) {
                    return false;
                }
                g_displayEffect = std::move(effect);
            }
            return true;
        }

        bool EnsureFxaaPipeline() {
            if (!EnsureDisplayPipeline()) {
                return false;
            }
            if (g_fxaaEffect == nullptr) {
                auto effect = std::make_unique<POST::PostEffect>();
                if (!effect->LoadPixelShader(
                        L"HIKARI/Shaders/Post_FXAA.hlsl")) {
                    return false;
                }
                g_fxaaEffect = std::move(effect);
            }
            return true;
        }

        bool EnsureTarget(
            InteractiveViewState& state,
            uint32_t width,
            uint32_t height,
            bool spatialAntiAliasing) {

            const bool primaryTargetsReady = state.target != nullptr &&
                state.target->IsInitialized() &&
                state.target->GetWidth() == static_cast<int>(width) &&
                state.target->GetHeight() == static_cast<int>(height) &&
                state.target->GetFormat() == kInteractiveViewFormat &&
                state.target->HasDepth() &&
                state.displayTarget != nullptr &&
                state.displayTarget->IsInitialized() &&
                state.displayTarget->GetWidth() == static_cast<int>(width) &&
                state.displayTarget->GetHeight() == static_cast<int>(height) &&
                state.displayTarget->GetFormat() == kInteractiveDisplayFormat &&
                !state.displayTarget->HasDepth();
            const bool aaTargetReady = !spatialAntiAliasing ||
                (state.antialiasedTarget != nullptr &&
                 state.antialiasedTarget->IsInitialized() &&
                 state.antialiasedTarget->GetWidth() ==
                     static_cast<int>(width) &&
                 state.antialiasedTarget->GetHeight() ==
                     static_cast<int>(height) &&
                 state.antialiasedTarget->GetFormat() ==
                     kInteractiveDisplayFormat &&
                 !state.antialiasedTarget->HasDepth());

            if (primaryTargetsReady && aaTargetReady) {

                state.target->UpdateContext(GFX::GetCurrentContext());
                state.displayTarget->UpdateContext(GFX::GetCurrentContext());
                if (state.antialiasedTarget != nullptr) {
                    state.antialiasedTarget->UpdateContext(
                        GFX::GetCurrentContext());
                }
                return true;
            }

            auto replacement = std::make_unique<RenderTarget2D>();
            replacement->UpdateContext(GFX::GetCurrentContext());
            if (!replacement->Init(
                    static_cast<int>(width),
                    static_cast<int>(height),
                    kInteractiveViewFormat,
                    true,
                    { 0.035f, 0.045f, 0.065f, 1.0f },
                    false,
                    false)) {
                return false;
            }
            replacement->SetDebugName("EditorInteractiveView");

            auto displayReplacement = std::make_unique<RenderTarget2D>();
            displayReplacement->UpdateContext(GFX::GetCurrentContext());
            if (!displayReplacement->Init(
                    static_cast<int>(width),
                    static_cast<int>(height),
                    kInteractiveDisplayFormat,
                    false,
                    { 0.0f, 0.0f, 0.0f, 1.0f },
                    false,
                    false)) {
                return false;
            }
            displayReplacement->SetDebugName("EditorInteractiveView.Display");
            std::unique_ptr<RenderTarget2D> aaReplacement{};
            if (spatialAntiAliasing) {
                aaReplacement = std::make_unique<RenderTarget2D>();
                aaReplacement->UpdateContext(GFX::GetCurrentContext());
                if (!aaReplacement->Init(
                        static_cast<int>(width),
                        static_cast<int>(height),
                        kInteractiveDisplayFormat,
                        false,
                        { 0.0f, 0.0f, 0.0f, 1.0f },
                        false,
                        false)) {
                    return false;
                }
                aaReplacement->SetDebugName(
                    "EditorInteractiveView.SpatialAA");
            }
            state.target = std::move(replacement);
            state.displayTarget = std::move(displayReplacement);
            state.antialiasedTarget = std::move(aaReplacement);
            ++state.targetGeneration;
            if (state.targetGeneration == 0) {
                state.targetGeneration = 1;
            }
            return true;
        }

        D3D12_GPU_DESCRIPTOR_HANDLE PublishOutputSrv(
            InteractiveViewState& state,
            RenderTarget2D& publishedTarget) {

            D3D12_GPU_DESCRIPTOR_HANDLE output{};
            if (publishedTarget.GetResource() == nullptr ||
                GFX::GetCurrentContext().device == nullptr) {
                return output;
            }

            UpdateRenderResourceDescriptorPoolContext(GFX::GetCurrentContext());
            const uint32_t frameSlot =
                GFX::GetCurrentContext().frameIndex % GFX::kFrameResourceCount;
            RenderResourceView& view = state.outputSrvs[frameSlot];
            if (!view.IsValid()) {
                view = AllocateTexture2DSrvDescriptor(
                    publishedTarget.GetResource(),
                    publishedTarget.GetFormat());
                if (!view.IsValid()) {
                    return output;
                }
                state.outputSrvGenerations[frameSlot] =
                    state.targetGeneration;
                state.outputSrvResources[frameSlot] =
                    publishedTarget.GetResource();
            } else if (state.outputSrvGenerations[frameSlot] !=
                           state.targetGeneration ||
                       state.outputSrvResources[frameSlot] !=
                           publishedTarget.GetResource()) {

                D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
                desc.Shader4ComponentMapping =
                    D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                desc.Format = publishedTarget.GetFormat();
                desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MostDetailedMip = 0;
                desc.Texture2D.MipLevels = 1;
                desc.Texture2D.PlaneSlice = 0;
                desc.Texture2D.ResourceMinLODClamp = 0.0f;
                GFX::GetCurrentContext().device->CreateShaderResourceView(
                    publishedTarget.GetResource(),
                    &desc,
                    view.cpu);
                state.outputSrvGenerations[frameSlot] =
                    state.targetGeneration;
                state.outputSrvResources[frameSlot] =
                    publishedTarget.GetResource();
            }
            return view.gpu;
        }

        void SetOutputStatus(
            InteractiveViewState& state,
            EditorInteractiveViewStatus status,
            EditorInteractiveViewFailure failure =
                EditorInteractiveViewFailure::None) {
            state.output.colorSrv = {};
            state.output.ready = false;
            state.output.status = status;
            state.output.failure = failure;
            state.output.sceneRevision = state.request.sceneRevision;
        }

        void FailOutput(
            InteractiveViewState& state,
            EditorInteractiveViewFailure failure) {
            SetOutputStatus(
                state,
                EditorInteractiveViewStatus::Failed,
                failure);
        }

        EditorInteractiveViewFailure ToViewFailure(
            MESHRENDERER::EditorInteractiveMeshRenderResult result) {

            using MeshResult =
                MESHRENDERER::EditorInteractiveMeshRenderResult;
            switch (result) {
            case MeshResult::RendererUnavailable:
                return EditorInteractiveViewFailure::
                    MeshRendererUnavailable;
            case MeshResult::MaterialFrameUnavailable:
                return EditorInteractiveViewFailure::
                    MaterialFrameUnavailable;
            case MeshResult::FrameResourcesUnavailable:
                return EditorInteractiveViewFailure::
                    FrameResourcesUnavailable;
            case MeshResult::SceneResidencyUnavailable:
                return EditorInteractiveViewFailure::
                    SceneResidencyUnavailable;
            case MeshResult::StylizedSurfacePassFailed:
                return EditorInteractiveViewFailure::
                    StylizedSurfacePassFailed;
            case MeshResult::ForwardPassFailed:
                return EditorInteractiveViewFailure::ForwardPassFailed;
            case MeshResult::StylizedGeometryPassFailed:
                return EditorInteractiveViewFailure::
                    StylizedGeometryPassFailed;
            case MeshResult::Ready:
            default:
                return EditorInteractiveViewFailure::None;
            }
        }

        std::array<float, 4> ResolveClearColor(
            EditorInteractiveShadingMode mode) {

            switch (mode) {
            case EditorInteractiveShadingMode::Neutral:
            case EditorInteractiveShadingMode::NeutralClay:
                return { 0.095f, 0.115f, 0.145f, 1.0f };
            case EditorInteractiveShadingMode::Unlit:
                return { 0.075f, 0.090f, 0.115f, 1.0f };
            case EditorInteractiveShadingMode::Lit:
            default:
                return { 0.035f, 0.045f, 0.065f, 1.0f };
            }
        }

        MESHRENDERER::EditorInteractiveRenderSettings ResolveRenderSettings(
            InteractiveViewState& state,
            const EditorInteractiveViewRequest& request,
            const EditorInteractiveStylizedPreparation& stylized) {

            MESHRENDERER::EditorInteractiveRenderSettings settings{};
            settings.neutralLighting =
                request.shadingMode ==
                    EditorInteractiveShadingMode::Neutral ||
                request.shadingMode ==
                    EditorInteractiveShadingMode::NeutralClay;
            settings.neutralClayMaterial =
                request.shadingMode ==
                    EditorInteractiveShadingMode::NeutralClay;
            if (request.shadingMode ==
                EditorInteractiveShadingMode::Unlit) {
                settings.debugView = RenderDebugView::BaseColor;
            } else if (settings.neutralClayMaterial) {
                settings.debugView = RenderDebugView::NeutralClay;
            }
            settings.renderGeometryOutline =
                stylized.renderGeometryOutline;
            settings.renderVisibilityOverlay =
                stylized.renderVisibilityOverlay;
            settings.writeStylizedFeatureMask =
                stylized.writeFeatureMask;
            settings.stylizedSurfaceBuffer =
                &state.stylizedView.GetSurfaceBuffer();
            settings.sceneDsv = state.target != nullptr
                ? state.target->GetDsvHandle()
                : D3D12_CPU_DESCRIPTOR_HANDLE{};
            settings.renderTargetUserData = state.target.get();
            settings.rebindTarget = [](void* userData) {
                auto* target = static_cast<RenderTarget2D*>(userData);
                if (target == nullptr || !target->IsInitialized()) {
                    return false;
                }
                target->Rebind();
                return true;
            };
            settings.sceneSourceOverride = request.sceneSourceOverride;
            settings.suppressSpatialLightingProviders =
                request.suppressSpatialLightingProviders;
            return settings;
        }

        bool ResolveDisplayTarget(
            InteractiveViewState& state,
            const SceneEnvironment& environment,
            RenderTarget2D& sourceTarget) {

            if (state.target == nullptr ||
                state.displayTarget == nullptr ||
                !EnsureDisplayPipeline()) {
                return false;
            }

            const float userExposure = std::clamp(
                state.request.displayExposure,
                0.25f,
                4.0f);
            bool toneMappingEnabled = true;
            float sceneExposure = 1.0f;
            float gamma = 2.2f;
            int toneMappingMode = 2;
            COLOR::DisplayColorFrameState displayColor{};
            COLOR::ExposureMode exposureMode =
                COLOR::ExposureMode::Manual;
            if (state.request.shadingMode ==
                    EditorInteractiveShadingMode::Lit) {
                toneMappingEnabled = environment.toneMapping.enabled;
                exposureMode = environment.toneMapping.exposureMode;
                sceneExposure =
                    (std::max)(0.0f, environment.toneMapping.exposure);
                gamma = (std::max)(0.01f, environment.toneMapping.gamma);
                toneMappingMode = environment.toneMapping.mode;
                displayColor = COLOR::BuildDisplayColorFrame(
                    environment.toneMapping.whiteBalanceTemperature,
                    environment.toneMapping.whiteBalanceTint,
                    environment.toneMapping.contrast,
                    environment.toneMapping.saturation);
            } else if (state.request.shadingMode ==
                    EditorInteractiveShadingMode::Unlit) {
                toneMappingEnabled = false;
                toneMappingMode = 0;
            }

            COLOR::ExposureFrameState exposureState =
                COLOR::BuildManualExposureFrame(
                    sceneExposure,
                    userExposure);
            if (exposureMode == COLOR::ExposureMode::Automatic) {
                exposureState = POST::PostSystem::GetExposureState();
                if (!exposureState.valid ||
                    exposureState.mode !=
                        COLOR::ExposureMode::Automatic) {
                    exposureState =
                        COLOR::BuildManualExposureFrame(sceneExposure);
                }
                exposureState.exposureMultiplier *= userExposure;
                exposureState.exposureCompensation *= userExposure;
            }
            POST::CommonParams parameters{};
            parameters.resolutionX = static_cast<float>(sourceTarget.GetWidth());
            parameters.resolutionY = static_cast<float>(sourceTarget.GetHeight());
            parameters.user[0] = {
                toneMappingEnabled ? 1.0f : 0.0f,
                exposureState.exposureMultiplier,
                gamma,
                static_cast<float>(toneMappingMode)
            };
            parameters.user[1] = {
                displayColor.whiteBalanceR,
                displayColor.whiteBalanceG,
                displayColor.whiteBalanceB,
                displayColor.contrast
            };
            parameters.user[2] = {
                displayColor.saturation,
                0.0f,
                1.25f,
                0.92f
            };
            parameters.user[2].y = 0.0f;
            parameters.user[3] = {
                0.025f,
                0.030f,
                0.045f,
                1.0f
            };

            state.displayTarget->BeginCapture(0.0f, 0.0f, 0.0f, 1.0f);
            const bool outputReady =
                g_displayQuad->SetOutputFormat(kInteractiveDisplayFormat);
            bool resolved = false;
            if (outputReady) {
                g_displayQuad->SetInputTexture(
                    sourceTarget.GetSrvHeap(),
                    sourceTarget.GetSrvGpu());
                g_displayEffect->ApplyCommonParams(parameters);
                resolved = g_displayEffect->BindAndDraw(*g_displayQuad);
            }
            state.displayTarget->EndCapture();
            return resolved;
        }

        bool ResolveSpatialAntiAliasing(
            InteractiveViewState& state,
            RenderTarget2D& sourceTarget) {
            if (!state.request.spatialAntiAliasing ||
                state.antialiasedTarget == nullptr ||
                !EnsureFxaaPipeline()) {
                return false;
            }

            const float width = static_cast<float>(
                (std::max)(1, sourceTarget.GetWidth()));
            const float height = static_cast<float>(
                (std::max)(1, sourceTarget.GetHeight()));
            POST::CommonParams parameters{};
            parameters.resolutionX = width;
            parameters.resolutionY = height;
            parameters.user[0] = {
                width, height, 1.0f / width, 1.0f / height
            };
            parameters.user[1] = {
                1.0f, 0.125f, 0.0312f, 0.75f
            };

            state.antialiasedTarget->BeginCapture(0.0f, 0.0f, 0.0f, 1.0f);
            const bool outputReady =
                g_displayQuad->SetOutputFormat(kInteractiveDisplayFormat);
            bool resolved = false;
            if (outputReady) {
                g_displayQuad->SetInputTexture(
                    sourceTarget.GetSrvHeap(), sourceTarget.GetSrvGpu());
                g_fxaaEffect->ApplyCommonParams(parameters);
                resolved = g_fxaaEffect->BindAndDraw(*g_displayQuad);
            }
            state.antialiasedTarget->EndCapture();
            return resolved;
        }

    } // namespace

    void SubmitRequest(const EditorInteractiveViewRequest& request) {
        InteractiveViewState& state = FindOrCreateState(request.viewId);
        const bool requestChanged = !state.hasRequest ||
            state.request.sceneRevision != request.sceneRevision ||
            state.request.sceneSourceOverride !=
                request.sceneSourceOverride ||
            state.request.width != request.width ||
            state.request.height != request.height ||
            state.request.spatialAntiAliasing !=
                request.spatialAntiAliasing;
        if (state.hasRequest &&
            state.request.sceneSourceOverride != request.sceneSourceOverride) {
            MESHRENDERER::ReleaseEditorInteractiveSceneSource(
                state.request.sceneSourceOverride);
        }
        const bool outputRouteChanged = state.hasRequest &&
            state.request.spatialAntiAliasing !=
                request.spatialAntiAliasing;
        state.request = request;
        if (outputRouteChanged) {
            ++state.targetGeneration;
            if (state.targetGeneration == 0u) {
                state.targetGeneration = 1u;
            }
        }
        state.output.viewId = request.viewId;
        state.hasRequest = true;
        if (requestChanged) {
            SetOutputStatus(
                state,
                EditorInteractiveViewStatus::Pending);
        }
    }

    void ClearRequest(RenderViewId viewId) {
        const auto found = g_views.find(viewId.value);
        if (found == g_views.end()) {
            return;
        }
        MESHRENDERER::ReleaseEditorInteractiveSceneSource(
            found->second->request.sceneSourceOverride);
        found->second->hasRequest = false;
        SetOutputStatus(
            *found->second,
            EditorInteractiveViewStatus::Idle);
    }

    bool RenderPending(
        const SceneEnvironment& environment,
        const LIGHTING::SceneLightingSet& lighting,
        uint64_t currentSceneRevision,
        ModelManager& modelManager,
        SkyManager& skyManager) {

        InteractiveViewState* selected = nullptr;
        uint32_t renderableViewCount = 0u;
        for (auto& [viewValue, state] : g_views) {
            (void)viewValue;
            if (!state->hasRequest) {
                continue;
            }
            if (!IsRenderableRequest(
                    state->request,
                    currentSceneRevision)) {
                SetOutputStatus(
                    *state,
                    state->request.visible
                        ? EditorInteractiveViewStatus::Pending
                        : EditorInteractiveViewStatus::Idle);
                continue;
            }
            ++renderableViewCount;
            const bool olderThanSelected =
                selected == nullptr ||
                state->lastRenderedSerial < selected->lastRenderedSerial;
            const bool sameAgeHigherPriority =
                selected != nullptr &&
                state->lastRenderedSerial == selected->lastRenderedSerial &&
                state->request.schedulingPriority >
                    selected->request.schedulingPriority;
            const bool sameAgeAndPriorityLowerId =
                selected != nullptr &&
                state->lastRenderedSerial == selected->lastRenderedSerial &&
                state->request.schedulingPriority ==
                    selected->request.schedulingPriority &&
                state->request.viewId.value < selected->request.viewId.value;
            if (olderThanSelected || sameAgeHigherPriority ||
                sameAgeAndPriorityLowerId) {
                selected = state.get();
            }
        }
        g_schedulerStats.renderableViewCount = renderableViewCount;
        if (selected == nullptr ||
            GFX::GetCurrentContext().cmdList == nullptr ||
            GFX::GetCurrentContext().device == nullptr) {
            return false;
        }

        ++g_scheduleSerial;
        if (g_scheduleSerial == 0u) {
            g_scheduleSerial = 1u;
        }
        selected->lastRenderedSerial = g_scheduleSerial;
        g_schedulerStats.scheduleSerial = g_scheduleSerial;
        g_schedulerStats.renderedViewId = selected->request.viewId.value;

        const uint32_t width = ClampExtent(selected->request.width);
        const uint32_t height = ClampExtent(selected->request.height);
        if (width == 0 ||
            height == 0 ||
            !EnsureTarget(*selected, width, height,
                          selected->request.spatialAntiAliasing)) {
            FailOutput(
                *selected,
                EditorInteractiveViewFailure::RenderTargetUnavailable);
            return false;
        }

        const std::array<float, 4> clearColor = ResolveClearColor(
            selected->request.shadingMode);
        selected->target->BeginCapture(
            clearColor[0],
            clearColor[1],
            clearColor[2],
            clearColor[3]);

        RenderViewContext view{};
        view.viewId = selected->request.viewId;
        view.purpose = selected->request.purpose;
        view.cameraFrame = &selected->request.cameraFrame;
        const SceneEnvironment& effectiveEnvironment =
            selected->request.environmentOverride != nullptr
                ? *selected->request.environmentOverride
                : environment;
        const LIGHTING::SceneLightingSet& effectiveLighting =
            selected->request.lightingOverride != nullptr
                ? *selected->request.lightingOverride
                : lighting;
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& sceneSource =
            selected->request.sceneSourceOverride != nullptr
                ? *selected->request.sceneSourceOverride
                : MESHRENDERER::GetActiveGpuDrivenSceneSource();
        const EditorInteractiveStylizedPreparation stylizedPreparation =
            selected->stylizedView.Prepare(sceneSource);
        // An authored preview environment is an isolation boundary.  A null
        // override means "use the process-global runtime IBL", which leaked
        // whichever scene/probe set rendered previously into material views.
        // Bind an explicit empty set first, then replace it only with the
        // complete DefaultSky set requested by Studio Inspect.
        const IBL::IblEnvironmentSource emptyIblEnvironment{};
        const IBL::IblEnvironmentSource* iblEnvironmentOverride =
            selected->request.environmentOverride != nullptr
                ? &emptyIblEnvironment
                : nullptr;
        if (selected->request.useDefaultSkyIbl) {
            const LIGHTING::DefaultSkyEnvironmentResult defaultSky =
                LIGHTING::EnsureDefaultSkyEnvironmentAvailable(
                skyManager);
            const IBL::IblEnvironmentSource& source =
                LIGHTING::GetDefaultSkyIblSource();
            if (defaultSky.iblLoaded && source.IsComplete()) {
                iblEnvironmentOverride = &source;
            }
        }
        SKYRENDERER::Render(
            GFX::GetCurrentContext(),
            selected->request.cameraFrame.camera,
            effectiveEnvironment,
            modelManager,
            skyManager);
        MESHRENDERER::EditorInteractiveRenderSettings renderSettings =
            ResolveRenderSettings(
                *selected,
                selected->request,
                stylizedPreparation);
        renderSettings.iblEnvironmentOverride =
            iblEnvironmentOverride;
        const MESHRENDERER::EditorInteractiveMeshRenderResult meshResult =
            MESHRENDERER::RenderEditorInteractiveOpaque(
                view,
                effectiveEnvironment,
                effectiveLighting,
                width,
                height,
                renderSettings);
        if (selected->request.drawDebug) {
            RENDERER3D::DEBUG::RenderAllForView(
                selected->request.viewId,
                selected->request.cameraFrame.camera,
                static_cast<float>(width),
                static_cast<float>(height));
        }
        selected->target->EndCapture();

        if (meshResult !=
            MESHRENDERER::EditorInteractiveMeshRenderResult::Ready) {
            (void)POST::PostSystem::RebindCurrentRenderTarget();
            FailOutput(
                *selected,
                ToViewFailure(meshResult));
            return false;
        }

        RenderTarget2D* displaySource = selected->target.get();
        if (!selected->stylizedView.Resolve(
                *selected->target,
                selected->targetGeneration,
                selected->request.viewId,
                selected->request.cameraFrame.camera,
                effectiveEnvironment,
                effectiveLighting,
                width,
                height,
                stylizedPreparation,
                displaySource)) {
            (void)POST::PostSystem::RebindCurrentRenderTarget();
            FailOutput(
                *selected,
                EditorInteractiveViewFailure::StylizedResolveFailed);
            return false;
        }

        if (displaySource == nullptr ||
            !ResolveDisplayTarget(
                *selected,
                effectiveEnvironment,
                *displaySource)) {
            (void)POST::PostSystem::RebindCurrentRenderTarget();
            FailOutput(
                *selected,
                EditorInteractiveViewFailure::DisplayResolveFailed);
            return false;
        }

        RenderTarget2D* publishedTarget = selected->displayTarget.get();
        if (ResolveSpatialAntiAliasing(
                *selected, *selected->displayTarget)) {
            publishedTarget = selected->antialiasedTarget.get();
        }
        const D3D12_GPU_DESCRIPTOR_HANDLE colorSrv =
            publishedTarget != nullptr
                ? PublishOutputSrv(*selected, *publishedTarget)
                : D3D12_GPU_DESCRIPTOR_HANDLE{};
        (void)POST::PostSystem::RebindCurrentRenderTarget();

        if (colorSrv.ptr == 0) {
            FailOutput(
                *selected,
                EditorInteractiveViewFailure::OutputDescriptorUnavailable);
            return false;
        }

        selected->output.viewId = selected->request.viewId;
        selected->output.colorSrv = colorSrv;
        selected->output.width = width;
        selected->output.height = height;
        selected->output.sceneRevision =
            selected->request.sceneSourceOverride != nullptr
                ? selected->request.sceneRevision
                : currentSceneRevision;
        ++selected->output.outputRevision;
        if (selected->output.outputRevision == 0) {
            selected->output.outputRevision = 1;
        }
        selected->output.ready = true;
        selected->output.status = EditorInteractiveViewStatus::Ready;
        selected->output.failure = EditorInteractiveViewFailure::None;
        return true;
    }

    EditorInteractiveViewOutput GetOutput(RenderViewId viewId) {
        const auto found = g_views.find(viewId.value);
        if (found == g_views.end()) {
            EditorInteractiveViewOutput output{};
            output.viewId = viewId;
            return output;
        }
        return found->second->output;
    }

    std::string_view GetEditorInteractiveViewFailureMessage(
        EditorInteractiveViewFailure failure) noexcept {
        switch (failure) {
        case EditorInteractiveViewFailure::RenderTargetUnavailable:
            return "Preview failed: render target unavailable";
        case EditorInteractiveViewFailure::MeshRendererUnavailable:
            return "Preview failed: mesh renderer unavailable";
        case EditorInteractiveViewFailure::MaterialFrameUnavailable:
            return "Preview waiting: material frame unavailable";
        case EditorInteractiveViewFailure::FrameResourcesUnavailable:
            return "Preview failed: frame resources unavailable";
        case EditorInteractiveViewFailure::SceneResidencyUnavailable:
            return "Preview waiting: model GPU residency incomplete";
        case EditorInteractiveViewFailure::StylizedSurfacePassFailed:
            return "Preview failed: NPR surface-mask pass";
        case EditorInteractiveViewFailure::ForwardPassFailed:
            return "Preview failed: no executable forward material partition";
        case EditorInteractiveViewFailure::StylizedGeometryPassFailed:
            return "Preview failed: outline geometry subpass";
        case EditorInteractiveViewFailure::StylizedResolveFailed:
            return "Preview failed: NPR / outline resolve";
        case EditorInteractiveViewFailure::DisplayResolveFailed:
            return "Preview failed: display resolve";
        case EditorInteractiveViewFailure::OutputDescriptorUnavailable:
            return "Preview failed: output descriptor unavailable";
        case EditorInteractiveViewFailure::None:
        default:
            return "Preview failed: unknown renderer error";
        }
    }

    EditorInteractiveSchedulerStats GetSchedulerStats() {
        return g_schedulerStats;
    }

    void Shutdown() {
        RENDERER3D::DEBUG::ShutdownEditorViewResources();
        for (auto& [viewValue, state] : g_views) {
            (void)viewValue;
            MESHRENDERER::ReleaseEditorInteractiveSceneSource(
                state->request.sceneSourceOverride);
            for (RenderResourceView& view : state->outputSrvs) {
                if (view.IsValid()) {
                    (void)ReleaseRenderResourceDescriptor(view);
                    view = {};
                }
            }
            state->outputSrvResources.fill(nullptr);
            state->stylizedView.Shutdown();
            state->target.reset();
            state->displayTarget.reset();
            state->antialiasedTarget.reset();
        }
        MESHRENDERER::ShutdownEditorInteractiveResources();
        g_views.clear();
        g_fxaaEffect.reset();
        g_displayEffect.reset();
        g_displayQuad.reset();
        ShutdownEditorInteractiveStylizedSharedResources();
        g_scheduleSerial = 0u;
        g_schedulerStats = {};
    }

} // namespace HIKARI::RENDER3D::EDITORVIEW

#endif
