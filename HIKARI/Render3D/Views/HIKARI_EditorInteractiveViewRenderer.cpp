#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"

#if defined(HIKARI_WITH_EDITOR)

#include <algorithm>
#include <array>
#include <memory>
#include <unordered_map>

#include "Gfx/HIKARI_GfxContext.h"
#include "HIKARI_Services.h"
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
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
            std::array<
                RenderResourceView,
                GFX::kFrameResourceCount> outputSrvs{};
            std::array<uint64_t, GFX::kFrameResourceCount>
                outputSrvGenerations{};
            uint64_t targetGeneration = 0;
            bool hasRequest = false;
        };

        std::unordered_map<uint64_t, std::unique_ptr<InteractiveViewState>>
            g_views{};
        std::unique_ptr<POST::QuadDrawer> g_displayQuad{};
        std::unique_ptr<POST::PostEffect> g_displayEffect{};

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
            POST::PostEffect::UpdateContext(SERVICES::gCtx);
            if (g_displayQuad == nullptr) {
                auto quad = std::make_unique<POST::QuadDrawer>();
                if (!quad->Init(SERVICES::gCtx)) {
                    return false;
                }
                g_displayQuad = std::move(quad);
            } else {
                g_displayQuad->UpdateContext(SERVICES::gCtx);
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

        bool EnsureTarget(
            InteractiveViewState& state,
            uint32_t width,
            uint32_t height) {

            if (state.target != nullptr &&
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
                !state.displayTarget->HasDepth()) {

                state.target->UpdateContext(SERVICES::gCtx);
                state.displayTarget->UpdateContext(SERVICES::gCtx);
                return true;
            }

            auto replacement = std::make_unique<RenderTarget2D>();
            replacement->UpdateContext(SERVICES::gCtx);
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
            displayReplacement->UpdateContext(SERVICES::gCtx);
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
            state.target = std::move(replacement);
            state.displayTarget = std::move(displayReplacement);
            ++state.targetGeneration;
            if (state.targetGeneration == 0) {
                state.targetGeneration = 1;
            }
            return true;
        }

        D3D12_GPU_DESCRIPTOR_HANDLE PublishOutputSrv(
            InteractiveViewState& state) {

            D3D12_GPU_DESCRIPTOR_HANDLE output{};
            if (state.displayTarget == nullptr ||
                state.displayTarget->GetResource() == nullptr ||
                SERVICES::gCtx.device == nullptr) {
                return output;
            }

            UpdateRenderResourceDescriptorPoolContext(SERVICES::gCtx);
            const uint32_t frameSlot =
                SERVICES::gCtx.frameIndex % GFX::kFrameResourceCount;
            RenderResourceView& view = state.outputSrvs[frameSlot];
            if (!view.IsValid()) {
                view = AllocateTexture2DSrvDescriptor(
                    state.displayTarget->GetResource(),
                    state.displayTarget->GetFormat());
                if (!view.IsValid()) {
                    return output;
                }
                state.outputSrvGenerations[frameSlot] =
                    state.targetGeneration;
            } else if (state.outputSrvGenerations[frameSlot] !=
                state.targetGeneration) {

                D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
                desc.Shader4ComponentMapping =
                    D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                desc.Format = state.displayTarget->GetFormat();
                desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MostDetailedMip = 0;
                desc.Texture2D.MipLevels = 1;
                desc.Texture2D.PlaneSlice = 0;
                desc.Texture2D.ResourceMinLODClamp = 0.0f;
                SERVICES::gCtx.device->CreateShaderResourceView(
                    state.displayTarget->GetResource(),
                    &desc,
                    view.cpu);
                state.outputSrvGenerations[frameSlot] =
                    state.targetGeneration;
            }
            return view.gpu;
        }

        void InvalidateOutput(InteractiveViewState& state) {
            state.output.colorSrv = {};
            state.output.ready = false;
        }

        std::array<float, 4> ResolveClearColor(
            EditorInteractiveShadingMode mode) {

            switch (mode) {
            case EditorInteractiveShadingMode::Neutral:
                return { 0.095f, 0.115f, 0.145f, 1.0f };
            case EditorInteractiveShadingMode::Unlit:
                return { 0.075f, 0.090f, 0.115f, 1.0f };
            case EditorInteractiveShadingMode::Lit:
            default:
                return { 0.035f, 0.045f, 0.065f, 1.0f };
            }
        }

        MESHRENDERER::EditorInteractiveRenderSettings ResolveRenderSettings(
            const EditorInteractiveViewRequest& request) {

            MESHRENDERER::EditorInteractiveRenderSettings settings{};
            settings.neutralLighting =
                request.shadingMode == EditorInteractiveShadingMode::Neutral;
            settings.debugView = request.shadingMode ==
                    EditorInteractiveShadingMode::Unlit
                ? RenderDebugView::BaseColor
                : RenderDebugView::None;
            settings.sceneSourceOverride = request.sceneSourceOverride;
            return settings;
        }

        bool ResolveDisplayTarget(
            InteractiveViewState& state,
            const SceneEnvironment& environment) {

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
            float exposure = userExposure;
            float gamma = 2.2f;
            int toneMappingMode = 2;
            if (state.request.shadingMode ==
                    EditorInteractiveShadingMode::Lit) {
                toneMappingEnabled = environment.toneMapping.enabled;
                exposure *= (std::max)(0.0f, environment.toneMapping.exposure);
                gamma = (std::max)(0.01f, environment.toneMapping.gamma);
                toneMappingMode = environment.toneMapping.mode;
            } else if (state.request.shadingMode ==
                    EditorInteractiveShadingMode::Unlit) {
                toneMappingEnabled = false;
                toneMappingMode = 0;
            }

            POST::CommonParams parameters{};
            parameters.resolutionX = static_cast<float>(state.target->GetWidth());
            parameters.resolutionY = static_cast<float>(state.target->GetHeight());
            parameters.user[0] = {
                toneMappingEnabled ? 1.0f : 0.0f,
                exposure,
                gamma,
                static_cast<float>(toneMappingMode)
            };

            state.displayTarget->BeginCapture(0.0f, 0.0f, 0.0f, 1.0f);
            const bool outputReady =
                g_displayQuad->SetOutputFormat(kInteractiveDisplayFormat);
            bool resolved = false;
            if (outputReady) {
                g_displayQuad->SetInputTexture(
                    state.target->GetSrvHeap(),
                    state.target->GetSrvGpu());
                g_displayEffect->ApplyCommonParams(parameters);
                resolved = g_displayEffect->BindAndDraw(*g_displayQuad);
            }
            state.displayTarget->EndCapture();
            return resolved;
        }

    } // namespace

    void SubmitRequest(const EditorInteractiveViewRequest& request) {
        InteractiveViewState& state = FindOrCreateState(request.viewId);
        state.request = request;
        state.output.viewId = request.viewId;
        state.hasRequest = true;
    }

    void ClearRequest(RenderViewId viewId) {
        const auto found = g_views.find(viewId.value);
        if (found == g_views.end()) {
            return;
        }
        found->second->hasRequest = false;
        InvalidateOutput(*found->second);
    }

    bool RenderPending(
        const SceneEnvironment& environment,
        uint64_t currentSceneRevision) {

        InteractiveViewState* selected = nullptr;
        for (auto& [viewValue, state] : g_views) {
            (void)viewValue;
            if (!state->hasRequest) {
                continue;
            }
            if (!IsRenderableRequest(
                    state->request,
                    currentSceneRevision)) {
                InvalidateOutput(*state);
                continue;
            }
            if (selected == nullptr ||
                state->request.viewId.value <
                    selected->request.viewId.value) {
                selected = state.get();
            }
        }
        if (selected == nullptr ||
            SERVICES::gCtx.cmdList == nullptr ||
            SERVICES::gCtx.device == nullptr) {
            return false;
        }

        const uint32_t width = ClampExtent(selected->request.width);
        const uint32_t height = ClampExtent(selected->request.height);
        if (width == 0 ||
            height == 0 ||
            !EnsureTarget(*selected, width, height)) {
            InvalidateOutput(*selected);
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
        view.purpose = RenderViewPurpose::EditorScene;
        view.cameraFrame = &selected->request.cameraFrame;
        const MESHRENDERER::EditorInteractiveRenderSettings renderSettings =
            ResolveRenderSettings(selected->request);
        (void)MESHRENDERER::RenderEditorInteractiveOpaque(
            view,
            environment,
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

        if (!ResolveDisplayTarget(*selected, environment)) {
            (void)POST::PostSystem::RebindCurrentRenderTarget();
            InvalidateOutput(*selected);
            return false;
        }

        const D3D12_GPU_DESCRIPTOR_HANDLE colorSrv =
            PublishOutputSrv(*selected);
        (void)POST::PostSystem::RebindCurrentRenderTarget();

        if (colorSrv.ptr == 0) {
            InvalidateOutput(*selected);
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

    void Shutdown() {
        RENDERER3D::DEBUG::ShutdownEditorViewResources();
        MESHRENDERER::ShutdownEditorInteractiveResources();
        for (auto& [viewValue, state] : g_views) {
            (void)viewValue;
            for (RenderResourceView& view : state->outputSrvs) {
                if (view.IsValid()) {
                    (void)ReleaseRenderResourceDescriptor(view);
                    view = {};
                }
            }
            state->target.reset();
            state->displayTarget.reset();
        }
        g_views.clear();
        g_displayEffect.reset();
        g_displayQuad.reset();
    }

} // namespace HIKARI::RENDER3D::EDITORVIEW

#endif
