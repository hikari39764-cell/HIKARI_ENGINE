#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Render3D/Views/HIKARI_EditorViewCapture.h"

#if defined(HIKARI_WITH_EDITOR)

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Gfx/HIKARI_GfxContext.h"
#include "Core/HIKARI_TimeService.h"
#include "Core/HIKARI_Logger.h"
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/Color/HIKARI_ColorGradingBake.h"
#include "Render3D/Color/HIKARI_ExposureState.h"
#include "Render3D/Color/HIKARI_ViewExposure.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/Lighting/HIKARI_DefaultSkyEnvironment.h"
#include "Render3D/Lighting/HIKARI_PanoramaSkyLayerStage.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"
#include "Render3D/Vfx/HIKARI_ParticleGpuDrivenPass.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewGuides.h"
#include "Render3D/Views/HIKARI_EditorInteractiveStylizedView.h"
#include "Vfx/Post/HIKARI_ColorGradingLut.h"
#include "Vfx/Post/HIKARI_PostCommon.h"
#include "Vfx/Post/HIKARI_PostEffect.h"
#include "Vfx/Post/HIKARI_PostProcessingStage.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI::RENDER3D::EDITORVIEW {

namespace {

struct GuidePassContext {
    const EditorInteractiveViewRequest* request = nullptr;
    uint32_t width = 0u;
    uint32_t height = 0u;
};

void RenderDepthAwareGuidePass(void* userData) {
    const auto* context = static_cast<const GuidePassContext*>(userData);
    if (context == nullptr || context->request == nullptr)
        return;
    INTERNAL::RenderGuideOverlay(*context->request, context->width, context->height);
}

constexpr uint32_t kMaxInteractiveViewExtent = 4096u;
constexpr DXGI_FORMAT kInteractiveViewFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr DXGI_FORMAT kInteractiveDisplayFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

struct InteractiveViewState {
    EditorInteractiveViewRequest request{};
    EditorInteractiveViewOutput output{};
    std::unique_ptr<RenderTarget2D> target{};
    std::unique_ptr<RenderTarget2D> opaqueSceneColorSnapshot{};
    std::unique_ptr<RenderTarget2D> displayTarget{};
    std::unique_ptr<RenderTarget2D> antialiasedTarget{};
    EditorInteractiveStylizedView stylizedView{};
    std::array<RenderResourceView, GFX::kFrameResourceCount> opaqueSceneColorSrvs{};
    std::array<RenderResourceView, GFX::kFrameResourceCount> sceneDepthSrvs{};
    std::array<uint64_t, GFX::kFrameResourceCount> opaqueSceneColorSrvGenerations{};
    std::array<uint64_t, GFX::kFrameResourceCount> sceneDepthSrvGenerations{};
    std::array<RenderResourceView, GFX::kFrameResourceCount> outputSrvs{};
    std::array<uint64_t, GFX::kFrameResourceCount> outputSrvGenerations{};
    std::array<ID3D12Resource*, GFX::kFrameResourceCount> outputSrvResources{};
    uint64_t targetGeneration = 0;
    uint64_t pendingSerial = 0u;
    uint64_t lastRenderedSerial = 0;
    uint64_t deviceGeneration = 0u;
    GFX::Context lastGraphicsContext{};
    bool pending = false;
    bool hasRequest = false;
};

std::unordered_map<uint64_t, std::unique_ptr<InteractiveViewState>> g_views{};
std::unique_ptr<POST::QuadDrawer> g_displayQuad{};
std::unique_ptr<POST::PostEffect> g_displayEffect{};
std::unique_ptr<POST::PostEffect> g_fxaaEffect{};
// プレビューでも本編と同じ bloom を通すための、ビュー専用の後処理段。
// PostProcessingStage はインスタンス化できるので、メイン描画経路に
// 触れず、bloom の実装も二重化しない (同じクラス・同じ設定を使う)。
std::unique_ptr<POST::PostProcessingStage> g_previewPostStage{};
constexpr uint32_t kMaximumContinuousRenderBurst = 3u;
uint64_t g_scheduleSerial = 0u;
uint64_t g_requestSerial = 0u;
uint32_t g_continuousRenderStreak = 0u;
EditorInteractiveSchedulerStats g_schedulerStats{};
std::unordered_map<uintptr_t, uint32_t> g_sceneSourceLeaseCounts{};

uint64_t NextNonZero(uint64_t value) noexcept {
    ++value;
    return value != 0u ? value : 1u;
}

void AcquireSceneSourceLease(const GPUDRIVEN::GpuDrivenSceneSource* source) {
    if (source == nullptr) {
        return;
    }
    ++g_sceneSourceLeaseCounts[reinterpret_cast<uintptr_t>(source)];
}

void ReleaseSceneSourceLease(const GPUDRIVEN::GpuDrivenSceneSource* source) {
    if (source == nullptr) {
        return;
    }
    const uintptr_t identity = reinterpret_cast<uintptr_t>(source);
    const auto found = g_sceneSourceLeaseCounts.find(identity);
    if (found == g_sceneSourceLeaseCounts.end()) {
        return;
    }
    if (found->second > 1u) {
        --found->second;
        return;
    }
    g_sceneSourceLeaseCounts.erase(found);
    MESHRENDERER::ReleaseEditorInteractiveSceneSource(source);
}

GFX::Context ResolveRetireContext(const InteractiveViewState& state) {
    const GFX::Context& current = GFX::GetCurrentContext();
    if (state.lastGraphicsContext.device != nullptr && (current.device == nullptr || current.deviceGeneration == 0u)) {
        // A cleared/partial context has no trustworthy fence owner.
        return {};
    }
    if (state.lastGraphicsContext.deviceGeneration != 0u && current.deviceGeneration != 0u &&
        state.lastGraphicsContext.deviceGeneration != current.deviceGeneration) {
        // There is no valid fence relationship across device
        // generations. Returning an empty context makes the caller
        // fail closed instead of enqueueing on an old queue pointer.
        return {};
    }
    if (state.lastGraphicsContext.device != nullptr && current.device != nullptr &&
        state.lastGraphicsContext.device != current.device) {
        return {};
    }
    if (state.lastGraphicsContext.deferredReleaseQueue != nullptr && current.deferredReleaseQueue != nullptr &&
        state.lastGraphicsContext.deferredReleaseQueue != current.deferredReleaseQueue) {
        return {};
    }
    if (state.lastGraphicsContext.device != nullptr || state.lastGraphicsContext.deferredReleaseQueue != nullptr) {
        return state.lastGraphicsContext;
    }
    return current;
}

void RetireDescriptorSet(std::array<RenderResourceView, GFX::kFrameResourceCount>& views, const GFX::Context& context,
                         const char* debugName) {
    for (RenderResourceView& view : views) {
        RetireRenderResourceDescriptor(view, context, debugName);
    }
}

void RetireViewResources(InteractiveViewState& state) {
    const GFX::Context current = GFX::GetCurrentContext();
    const GFX::Context context = ResolveRetireContext(state);
    const bool contextChanged =
        context.device == nullptr && state.lastGraphicsContext.device != nullptr &&
        ((current.device == nullptr || current.deviceGeneration == 0u) ||
         (current.deviceGeneration != 0u && state.lastGraphicsContext.deviceGeneration != 0u &&
          current.deviceGeneration != state.lastGraphicsContext.deviceGeneration) ||
         (current.device != nullptr && current.device != state.lastGraphicsContext.device) ||
         (current.deferredReleaseQueue != nullptr && state.lastGraphicsContext.deferredReleaseQueue != nullptr &&
          current.deferredReleaseQueue != state.lastGraphicsContext.deferredReleaseQueue));

    if (contextChanged) {
        // RenderTarget2D keeps its own context copy. Clear it before
        // destruction when the old device is no longer compatible;
        // otherwise its COM retirement could dereference an old
        // deferred-release queue.
        if (state.target != nullptr) {
            state.target->UpdateContext({});
        }
        if (state.opaqueSceneColorSnapshot != nullptr) {
            state.opaqueSceneColorSnapshot->UpdateContext({});
        }
        if (state.displayTarget != nullptr) {
            state.displayTarget->UpdateContext({});
        }
        if (state.antialiasedTarget != nullptr) {
            state.antialiasedTarget->UpdateContext({});
        }
    }

    // StylizedView owns its composite target and SceneSurface
    // descriptors. Its shutdown is deliberately per view; the
    // descriptor owner defers the actual slot reuse to this frame's
    // fence just like the explicit view-owned descriptor arrays.
    state.stylizedView.Shutdown();
    RetireDescriptorSet(state.outputSrvs, context, "EditorInteractiveView.OutputSRV");
    RetireDescriptorSet(state.opaqueSceneColorSrvs, context, "EditorInteractiveView.OpaqueSceneColorSRV");
    RetireDescriptorSet(state.sceneDepthSrvs, context, "EditorInteractiveView.SceneDepthSRV");
    state.outputSrvGenerations.fill(0u);
    state.opaqueSceneColorSrvGenerations.fill(0u);
    state.sceneDepthSrvGenerations.fill(0u);
    state.outputSrvResources.fill(nullptr);

    // RenderTarget2D::Finalize transfers its COM resources and
    // private heaps to the same fence-safe deferred release queue.
    state.target.reset();
    state.opaqueSceneColorSnapshot.reset();
    state.displayTarget.reset();
    state.antialiasedTarget.reset();
    state.lastGraphicsContext = {};
    state.deviceGeneration = 0u;
    state.output.colorSrv = {};
    state.output.ready = false;
    state.output.width = 0u;
    state.output.height = 0u;
    state.output.sceneRevision = 0u;
}

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

bool IsRenderableRequest(const EditorInteractiveViewRequest& request, uint64_t currentSceneRevision) {
    // Primary-scene requests are submitted by ImGui after the scene
    // render for the current frame has already completed.  The
    // request therefore normally describes the preceding UI frame.
    // Inspector/sequencer edits may advance the document revision
    // again before RenderPending consumes it.  Accept that monotonic
    // lag and render the current active scene; a request from a newer
    // revision is still rejected so scene replacement/reset cannot
    // reuse a request from the previous document generation.
    const bool sceneRevisionIsConsumable =
        request.sceneSourceOverride != nullptr || request.sceneRevision <= currentSceneRevision;
    return request.viewId.value != 0 && request.visible && request.width != 0 && request.height != 0 &&
           sceneRevisionIsConsumable && request.cameraFrame.valid;
}

bool RequestsDiffer(const EditorInteractiveViewRequest& previous,
                    const EditorInteractiveViewRequest& current) noexcept {
    return previous.purpose != current.purpose ||
           previous.cameraFrame.sourceCameraObjectId != current.cameraFrame.sourceCameraObjectId ||
           previous.cameraFrame.revision != current.cameraFrame.revision ||
           previous.cameraFrame.cameraCut != current.cameraFrame.cameraCut ||
           previous.cameraFrame.continuousCameraSource != current.cameraFrame.continuousCameraSource ||
           previous.cameraFrame.projectionChanged != current.cameraFrame.projectionChanged ||
           previous.width != current.width || previous.height != current.height ||
           previous.sceneRevision != current.sceneRevision || previous.contentRevision != current.contentRevision ||
           previous.visible != current.visible || previous.drawDebug != current.drawDebug ||
           previous.continuousRealtime != current.continuousRealtime ||
           previous.schedulingPriority != current.schedulingPriority || previous.shadingMode != current.shadingMode ||
           previous.displayExposure != current.displayExposure ||
           previous.sceneSourceOverride != current.sceneSourceOverride ||
           previous.environmentOverride != current.environmentOverride ||
           previous.lightingOverride != current.lightingOverride ||
           previous.particleInputOverride != current.particleInputOverride ||
           previous.guideOverlay.showGroundGrid != current.guideOverlay.showGroundGrid ||
           previous.guideOverlay.showWorldAxes != current.guideOverlay.showWorldAxes ||
           previous.suppressSpatialLightingProviders != current.suppressSpatialLightingProviders ||
           previous.useDefaultSkyIbl != current.useDefaultSkyIbl ||
           previous.backgroundColor != current.backgroundColor ||
           previous.spatialAntiAliasing != current.spatialAntiAliasing;
}

uint32_t ClampExtent(uint32_t value) { return (std::min)(value, kMaxInteractiveViewExtent); }

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
        if (!effect->LoadPixelShader({.programId = "HIKARI.EngineShader.Post_ToneMappingPS"})) {
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
        if (!effect->LoadPixelShader({.programId = "HIKARI.EngineShader.Post_FXAA"})) {
            return false;
        }
        g_fxaaEffect = std::move(effect);
    }
    return true;
}

// プレビュー用の後処理段。本編の PostSystem は単一レンダー面に結び付いて
// いるため共有できず、ここで同じクラスをもう 1 つ持つ。設定 (bloom 閾値・
// 強度・半径) はシーン環境から渡すので、見た目は本編と揃う。
bool EnsurePreviewPostStage() {
    if (g_previewPostStage == nullptr) {
        g_previewPostStage = std::make_unique<POST::PostProcessingStage>();
    }
    g_previewPostStage->UpdateContext(GFX::GetCurrentContext());
    return true;
}

bool EnsureTarget(InteractiveViewState& state, uint32_t width, uint32_t height, bool spatialAntiAliasing) {
    const GFX::Context graphics = GFX::GetCurrentContext();
    if (state.deviceGeneration != 0u && graphics.deviceGeneration != 0u &&
        state.deviceGeneration != graphics.deviceGeneration) {
        // A device-generation change invalidates every raw resource
        // and SRV behind this view. Retire the old set before the
        // first command on the new device and keep the request queued
        // for a fresh target publication.
        RetireViewResources(state);
        state.pending = state.hasRequest;
        state.output.requestedRevision = state.request.sceneRevision;
        state.output.status =
            state.hasRequest ? EditorInteractiveViewStatus::Pending : EditorInteractiveViewStatus::Idle;
    }

    const bool primaryTargetsReady =
        state.target != nullptr && state.target->IsInitialized() &&
        state.target->GetWidth() == static_cast<int>(width) && state.target->GetHeight() == static_cast<int>(height) &&
        state.target->GetFormat() == kInteractiveViewFormat && state.target->HasDepth() &&
        state.opaqueSceneColorSnapshot != nullptr && state.opaqueSceneColorSnapshot->IsInitialized() &&
        state.opaqueSceneColorSnapshot->GetWidth() == static_cast<int>(width) &&
        state.opaqueSceneColorSnapshot->GetHeight() == static_cast<int>(height) &&
        state.opaqueSceneColorSnapshot->GetFormat() == kInteractiveViewFormat &&
        !state.opaqueSceneColorSnapshot->HasDepth() && state.displayTarget != nullptr &&
        state.displayTarget->IsInitialized() && state.displayTarget->GetWidth() == static_cast<int>(width) &&
        state.displayTarget->GetHeight() == static_cast<int>(height) &&
        state.displayTarget->GetFormat() == kInteractiveDisplayFormat && !state.displayTarget->HasDepth();
    const bool aaTargetReady =
        !spatialAntiAliasing ||
        (state.antialiasedTarget != nullptr && state.antialiasedTarget->IsInitialized() &&
         state.antialiasedTarget->GetWidth() == static_cast<int>(width) &&
         state.antialiasedTarget->GetHeight() == static_cast<int>(height) &&
         state.antialiasedTarget->GetFormat() == kInteractiveDisplayFormat && !state.antialiasedTarget->HasDepth());

    if (primaryTargetsReady && aaTargetReady) {
        state.target->UpdateContext(graphics);
        state.opaqueSceneColorSnapshot->UpdateContext(graphics);
        state.displayTarget->UpdateContext(graphics);
        if (state.antialiasedTarget != nullptr) {
            state.antialiasedTarget->UpdateContext(graphics);
        }
        state.lastGraphicsContext = graphics;
        state.deviceGeneration = graphics.deviceGeneration;
        return true;
    }

    auto replacement = std::make_unique<RenderTarget2D>();
    replacement->UpdateContext(graphics);
    if (!replacement->Init(static_cast<int>(width), static_cast<int>(height), kInteractiveViewFormat, true,
                           {0.035f, 0.045f, 0.065f, 1.0f}, false, false)) {
        return false;
    }
    replacement->SetDebugName("EditorInteractiveView");

    auto sceneColorReplacement = std::make_unique<RenderTarget2D>();
    sceneColorReplacement->UpdateContext(graphics);
    if (!sceneColorReplacement->Init(static_cast<int>(width), static_cast<int>(height), kInteractiveViewFormat, false,
                                     {0.0f, 0.0f, 0.0f, 1.0f}, false, false)) {
        return false;
    }
    sceneColorReplacement->SetDebugName("EditorInteractiveView.OpaqueSceneColor");

    auto displayReplacement = std::make_unique<RenderTarget2D>();
    displayReplacement->UpdateContext(graphics);
    if (!displayReplacement->Init(static_cast<int>(width), static_cast<int>(height), kInteractiveDisplayFormat, false,
                                  {0.0f, 0.0f, 0.0f, 1.0f}, false, false)) {
        return false;
    }
    displayReplacement->SetDebugName("EditorInteractiveView.Display");
    std::unique_ptr<RenderTarget2D> aaReplacement{};
    if (spatialAntiAliasing) {
        aaReplacement = std::make_unique<RenderTarget2D>();
        aaReplacement->UpdateContext(graphics);
        if (!aaReplacement->Init(static_cast<int>(width), static_cast<int>(height), kInteractiveDisplayFormat, false,
                                 {0.0f, 0.0f, 0.0f, 1.0f}, false, false)) {
            return false;
        }
        aaReplacement->SetDebugName("EditorInteractiveView.SpatialAA");
    }
    state.target = std::move(replacement);
    state.opaqueSceneColorSnapshot = std::move(sceneColorReplacement);
    state.displayTarget = std::move(displayReplacement);
    state.antialiasedTarget = std::move(aaReplacement);
    // 新しいターゲットにはまだ表示用の画像がない。
    // 旧 outputRevision の ready を引き継ぐと空の画像を取得してしまう。
    state.output.ready = false;
    ++state.targetGeneration;
    if (state.targetGeneration == 0) {
        state.targetGeneration = 1;
    }
    state.lastGraphicsContext = graphics;
    state.deviceGeneration = graphics.deviceGeneration;
    return true;
}

D3D12_GPU_DESCRIPTOR_HANDLE PublishOutputSrv(InteractiveViewState& state, RenderTarget2D& publishedTarget) {
    D3D12_GPU_DESCRIPTOR_HANDLE output{};
    if (publishedTarget.GetResource() == nullptr || GFX::GetCurrentContext().device == nullptr) {
        return output;
    }

    UpdateRenderResourceDescriptorPoolContext(GFX::GetCurrentContext());
    const uint32_t frameSlot = GFX::GetCurrentContext().frameIndex % GFX::kFrameResourceCount;
    RenderResourceView& view = state.outputSrvs[frameSlot];
    if (!view.IsValid()) {
        view = AllocateTexture2DSrvDescriptor(publishedTarget.GetResource(), publishedTarget.GetFormat());
        if (!view.IsValid()) {
            return output;
        }
        state.outputSrvGenerations[frameSlot] = state.targetGeneration;
        state.outputSrvResources[frameSlot] = publishedTarget.GetResource();
    } else if (state.outputSrvGenerations[frameSlot] != state.targetGeneration ||
               state.outputSrvResources[frameSlot] != publishedTarget.GetResource()) {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Format = publishedTarget.GetFormat();
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MostDetailedMip = 0;
        desc.Texture2D.MipLevels = 1;
        desc.Texture2D.PlaneSlice = 0;
        desc.Texture2D.ResourceMinLODClamp = 0.0f;
        GFX::GetCurrentContext().device->CreateShaderResourceView(publishedTarget.GetResource(), &desc, view.cpu);
        state.outputSrvGenerations[frameSlot] = state.targetGeneration;
        state.outputSrvResources[frameSlot] = publishedTarget.GetResource();
    }
    return view.gpu;
}

D3D12_GPU_DESCRIPTOR_HANDLE PublishInteractiveTextureSrv(
    std::array<RenderResourceView, GFX::kFrameResourceCount>& views,
    std::array<uint64_t, GFX::kFrameResourceCount>& generations, uint64_t targetGeneration, ID3D12Resource* resource,
    DXGI_FORMAT format) {
    D3D12_GPU_DESCRIPTOR_HANDLE output{};
    const GFX::Context& graphics = GFX::GetCurrentContext();
    if (resource == nullptr || graphics.device == nullptr) {
        return output;
    }

    UpdateRenderResourceDescriptorPoolContext(graphics);
    const uint32_t frameSlot = graphics.frameIndex % GFX::kFrameResourceCount;
    RenderResourceView& view = views[frameSlot];
    if (!view.IsValid()) {
        view = AllocateTexture2DSrvDescriptor(resource, format);
        if (!view.IsValid()) {
            return output;
        }
        generations[frameSlot] = targetGeneration;
    } else if (generations[frameSlot] != targetGeneration) {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Format = format;
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MostDetailedMip = 0;
        desc.Texture2D.MipLevels = 1;
        desc.Texture2D.PlaneSlice = 0;
        desc.Texture2D.ResourceMinLODClamp = 0.0f;
        graphics.device->CreateShaderResourceView(resource, &desc, view.cpu);
        generations[frameSlot] = targetGeneration;
    }
    return view.gpu;
}

RENDER3D::RenderTextureView BuildInteractiveSceneDepthView(InteractiveViewState& state) {
    if (state.target == nullptr || !state.target->HasDepth()) {
        return {};
    }
    const D3D12_GPU_DESCRIPTOR_HANDLE srv =
        PublishInteractiveTextureSrv(state.sceneDepthSrvs, state.sceneDepthSrvGenerations, state.targetGeneration,
                                     state.target->GetDepthResource(), DXGI_FORMAT_R32_FLOAT);
    return {state.target->GetDepthResource(),
            srv,
            DXGI_FORMAT_R32_FLOAT,
            static_cast<uint32_t>(state.target->GetWidth()),
            static_cast<uint32_t>(state.target->GetHeight()),
            1u,
            state.target->GetDepthState(),
            state.target->GetDepthResource() != nullptr && srv.ptr != 0u};
}

RENDER3D::RenderTextureView CaptureInteractiveSceneColor(void* userData) {
    auto* state = static_cast<InteractiveViewState*>(userData);
    const GFX::Context& graphics = GFX::GetCurrentContext();
    if (state == nullptr || state->target == nullptr || state->opaqueSceneColorSnapshot == nullptr ||
        graphics.cmdList == nullptr || state->target->GetResource() == nullptr ||
        state->opaqueSceneColorSnapshot->GetResource() == nullptr) {
        return {};
    }

    // Opaque + sky の結果を独立 resource に固定し、後段の
    // depth-aware / transparent pass が同じ RTV を sample しない。
    state->target->TransitionColor(D3D12_RESOURCE_STATE_COPY_SOURCE);
    state->opaqueSceneColorSnapshot->TransitionColor(D3D12_RESOURCE_STATE_COPY_DEST);
    graphics.cmdList->CopyResource(state->opaqueSceneColorSnapshot->GetResource(), state->target->GetResource());
    state->opaqueSceneColorSnapshot->TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    state->target->TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);

    const D3D12_GPU_DESCRIPTOR_HANDLE srv = PublishInteractiveTextureSrv(
        state->opaqueSceneColorSrvs, state->opaqueSceneColorSrvGenerations, state->targetGeneration,
        state->opaqueSceneColorSnapshot->GetResource(), state->opaqueSceneColorSnapshot->GetFormat());
    return {state->opaqueSceneColorSnapshot->GetResource(),
            srv,
            state->opaqueSceneColorSnapshot->GetFormat(),
            static_cast<uint32_t>(state->opaqueSceneColorSnapshot->GetWidth()),
            static_cast<uint32_t>(state->opaqueSceneColorSnapshot->GetHeight()),
            1u,
            state->opaqueSceneColorSnapshot->GetColorState(),
            srv.ptr != 0u};
}

bool RebindInteractiveTarget(void* userData) {
    auto* state = static_cast<InteractiveViewState*>(userData);
    if (state == nullptr || state->target == nullptr || !state->target->IsInitialized()) {
        return false;
    }
    state->target->Rebind();
    return true;
}

bool BeginInteractiveDepthRead(void* userData) {
    auto* state = static_cast<InteractiveViewState*>(userData);
    return state != nullptr && state->target != nullptr && state->target->BeginDepthRead();
}

void EndInteractiveDepthRead(void* userData) {
    auto* state = static_cast<InteractiveViewState*>(userData);
    if (state != nullptr && state->target != nullptr) {
        state->target->EndDepthRead();
    }
}

void SetOutputStatus(InteractiveViewState& state, EditorInteractiveViewStatus status,
                     EditorInteractiveViewFailure failure = EditorInteractiveViewFailure::None) {
    state.output.status = status;
    state.output.failure = failure;
    state.output.requestedRevision = state.request.sceneRevision;

    // A status transition describes the receipt; it does not erase a
    // last-completed image. ClearRequest and a device-generation reset
    // are the only destructive publication boundaries.
    if (!state.output.ready) {
        state.output.colorSrv = {};
        state.output.ready = false;
    }
}

void MarkOutputPending(InteractiveViewState& state) {
    // A newer request is queued after the render phase of the current
    // frame.  Keep the last successfully published texture alive
    // until its replacement is ready so continuously edited tool
    // views do not alternate between a valid image and an empty
    // placeholder. ClearRequest and device replacement remain
    // destructive publication boundaries.
    state.pending = true;
    state.output.status = EditorInteractiveViewStatus::Pending;
    state.output.failure = EditorInteractiveViewFailure::None;
    state.output.requestedRevision = state.request.sceneRevision;
}

bool ShouldRetryFailure(EditorInteractiveViewFailure failure) noexcept {
    return failure != EditorInteractiveViewFailure::None;
}

void FailOutput(InteractiveViewState& state, EditorInteractiveViewFailure failure) {
    // Every renderer stage can recover after a later resource or
    // device/frame transition. Keep the request retryable; the
    // scheduler's realtime burst limit bounds repeated attempts and
    // still gives static views a deterministic turn.
    state.pending = ShouldRetryFailure(failure);
    state.pendingSerial = state.pending ? g_requestSerial = NextNonZero(g_requestSerial) : 0u;
    SetOutputStatus(state, EditorInteractiveViewStatus::Failed, failure);
}

EditorInteractiveViewFailure ToViewFailure(MESHRENDERER::EditorInteractiveMeshRenderResult result) {
    using MeshResult = MESHRENDERER::EditorInteractiveMeshRenderResult;
    switch (result) {
    case MeshResult::RendererUnavailable: return EditorInteractiveViewFailure::MeshRendererUnavailable;
    case MeshResult::MaterialFrameUnavailable: return EditorInteractiveViewFailure::MaterialFrameUnavailable;
    case MeshResult::FrameResourcesUnavailable: return EditorInteractiveViewFailure::FrameResourcesUnavailable;
    case MeshResult::SceneResidencyUnavailable: return EditorInteractiveViewFailure::SceneResidencyUnavailable;
    case MeshResult::StylizedSurfacePassFailed: return EditorInteractiveViewFailure::StylizedSurfacePassFailed;
    case MeshResult::ForwardPassFailed: return EditorInteractiveViewFailure::ForwardPassFailed;
    case MeshResult::StylizedGeometryPassFailed: return EditorInteractiveViewFailure::StylizedGeometryPassFailed;
    case MeshResult::Ready:
    default: return EditorInteractiveViewFailure::None;
    }
}

std::array<float, 4> ResolveClearColor(EditorInteractiveShadingMode mode) {
    switch (mode) {
    case EditorInteractiveShadingMode::Neutral:
    case EditorInteractiveShadingMode::NeutralClay: return {0.095f, 0.115f, 0.145f, 1.0f};
    case EditorInteractiveShadingMode::Unlit: return {0.075f, 0.090f, 0.115f, 1.0f};
    case EditorInteractiveShadingMode::Lit:
    default: return {0.035f, 0.045f, 0.065f, 1.0f};
    }
}

MESHRENDERER::EditorInteractiveRenderSettings ResolveRenderSettings(
    InteractiveViewState& state, const EditorInteractiveViewRequest& request,
    const EditorInteractiveStylizedPreparation& stylized) {
    MESHRENDERER::EditorInteractiveRenderSettings settings{};
    settings.neutralLighting = request.shadingMode == EditorInteractiveShadingMode::Neutral ||
                               request.shadingMode == EditorInteractiveShadingMode::NeutralClay;
    settings.neutralClayMaterial = request.shadingMode == EditorInteractiveShadingMode::NeutralClay;
    if (request.shadingMode == EditorInteractiveShadingMode::Unlit) {
        settings.debugView = RenderDebugView::BaseColor;
    } else if (settings.neutralClayMaterial) {
        settings.debugView = RenderDebugView::NeutralClay;
    }
    settings.renderGeometryOutline = stylized.renderGeometryOutline;
    settings.renderVisibilityOverlay = stylized.renderVisibilityOverlay;
    settings.writeStylizedFeatureMask = stylized.writeFeatureMask;
    settings.stylizedSurfaceBuffer = &state.stylizedView.GetSurfaceBuffer();
    settings.passResources.compositionDepth =
        state.target != nullptr ? state.target->GetReadOnlyDsvHandle() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    settings.sceneDsv = state.target != nullptr ? state.target->GetDsvHandle() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    settings.passResources.sceneDepth = BuildInteractiveSceneDepthView(state);
    settings.targetAccess.userData = &state;
    settings.targetAccess.rebind = &RebindInteractiveTarget;
    settings.targetAccess.beginDepthRead = &BeginInteractiveDepthRead;
    settings.targetAccess.endDepthRead = &EndInteractiveDepthRead;
    settings.captureSceneColor = &CaptureInteractiveSceneColor;
    settings.sceneSourceOverride = request.sceneSourceOverride;
    settings.suppressSpatialLightingProviders = request.suppressSpatialLightingProviders;
    return settings;
}

ToneMappingSettings ResolveViewToneMapping(const InteractiveViewState& state, const SceneEnvironment& environment) {
    ToneMappingSettings toneMapping{};
    if (state.request.shadingMode == EditorInteractiveShadingMode::Lit) {
        toneMapping = environment.toneMapping;
    } else if (state.request.shadingMode == EditorInteractiveShadingMode::Unlit) {
        toneMapping.curve = ToneCurve::None;
    }
    return toneMapping;
}

// ビューの露出。HDR へ書く前に決め、描画とトーンマッピングの両方で同じ値を使う。
COLOR::ExposureFrameState ResolveViewExposureFrame(const InteractiveViewState& state,
                                                   const SceneEnvironment& environment) {
    const float userExposure = std::clamp(state.request.displayExposure, 0.25f, 4.0f);
    // 照明を使わない表示は表示参照の値をそのまま出す。
    if (state.request.shadingMode != EditorInteractiveShadingMode::Lit) {
        return COLOR::BuildManualExposureFrame(1.0f, 0.0f, userExposure);
    }
    if (environment.exposure.mode == COLOR::ExposureMode::Automatic) {
        // 自動露出は主ビューの測光を借りる。視点ごとの補正は残差側だけに掛ける。
        COLOR::ExposureFrameState exposureState = POST::PostSystem::GetExposureState();
        if (exposureState.valid && exposureState.mode == COLOR::ExposureMode::Automatic) {
            exposureState.exposure *= userExposure;
            return exposureState;
        }
    }
    return COLOR::BuildManualExposureFrame(COLOR::ResolveManualExposure(environment.exposure), 0.0f, userExposure);
}

bool ResolveDisplayTarget(InteractiveViewState& state, const SceneEnvironment& environment,
                          const COLOR::ExposureFrameState& exposureState, RenderTarget2D& sourceTarget) {
    if (state.target == nullptr || state.displayTarget == nullptr || !EnsureDisplayPipeline()) {
        return false;
    }

    const ToneMappingSettings toneMapping = ResolveViewToneMapping(state, environment);
    // 照明を見るビューは主ビューと同じ環境の色調整を掛ける。
    ID3D12Resource* gradingLut = state.request.shadingMode == EditorInteractiveShadingMode::Lit
                                     ? POST::PostSystem::GetColorGradingLut()
                                     : nullptr;

    state.displayTarget->BeginCapture(0.0f, 0.0f, 0.0f, 1.0f);
    const bool outputReady = g_displayQuad->SetOutputFormat(kInteractiveDisplayFormat);
    bool resolved = false;
    if (outputReady) {
        const POST::ToneMappingInputs inputs(GFX::GetCurrentContext(), *g_displayQuad, sourceTarget, gradingLut,
                                             nullptr);
        if (!inputs) {
            g_displayQuad->SetInputTexture(sourceTarget.GetSrvHeap(), sourceTarget.GetSrvGpu());
        }
        POST::CommonParams parameters{};
        parameters.resolutionX = static_cast<float>(sourceTarget.GetWidth());
        parameters.resolutionY = static_cast<float>(sourceTarget.GetHeight());
        POST::WriteToneMappingParameters(parameters, toneMapping, COLOR::ResidualExposure(exposureState), 0.0f, 0.0f,
                                         inputs && gradingLut ? COLOR::kColorGradingLutSize : 0u);
        g_displayEffect->ApplyCommonParams(parameters);
        resolved = g_displayEffect->BindAndDraw(*g_displayQuad);
    }
    state.displayTarget->EndCapture();
    return resolved;
}

bool ResolveSpatialAntiAliasing(InteractiveViewState& state, RenderTarget2D& sourceTarget) {
    if (!state.request.spatialAntiAliasing || state.antialiasedTarget == nullptr || !EnsureFxaaPipeline()) {
        return false;
    }

    POST::CommonParams parameters{};
    POST::WriteFxaaParameters(parameters, sourceTarget.GetWidth(), sourceTarget.GetHeight());

    state.antialiasedTarget->BeginCapture(0.0f, 0.0f, 0.0f, 1.0f);
    const bool outputReady = g_displayQuad->SetOutputFormat(kInteractiveDisplayFormat);
    bool resolved = false;
    if (outputReady) {
        g_displayQuad->SetInputTexture(sourceTarget.GetSrvHeap(), sourceTarget.GetSrvGpu());
        g_fxaaEffect->ApplyCommonParams(parameters);
        resolved = g_fxaaEffect->BindAndDraw(*g_displayQuad);
    }
    state.antialiasedTarget->EndCapture();
    return resolved;
}

} // namespace

void SubmitRequest(const EditorInteractiveViewRequest& request) {
    if (request.viewId.value == 0u) {
        return;
    }

    InteractiveViewState& state = FindOrCreateState(request.viewId);
    const bool requestChanged = !state.hasRequest || RequestsDiffer(state.request, request);
    const bool sourceChanged = state.hasRequest && state.request.sceneSourceOverride != request.sceneSourceOverride;
    if (sourceChanged) {
        ReleaseSceneSourceLease(state.request.sceneSourceOverride);
    }
    if (!state.hasRequest || sourceChanged) {
        AcquireSceneSourceLease(request.sceneSourceOverride);
    }

    const bool outputRouteChanged =
        state.hasRequest && state.request.spatialAntiAliasing != request.spatialAntiAliasing;
    state.request = request;
    if (outputRouteChanged) {
        state.targetGeneration = NextNonZero(state.targetGeneration);
    }
    state.output.viewId = request.viewId;
    state.output.requestedRevision = state.request.sceneRevision;
    state.hasRequest = true;
    if (requestChanged) {
        state.pendingSerial = g_requestSerial = NextNonZero(g_requestSerial);
        MarkOutputPending(state);
    }
}

void ClearRequest(RenderViewId viewId) {
    const auto found = g_views.find(viewId.value);
    if (found == g_views.end()) {
        return;
    }
    InteractiveViewState& state = *found->second;
    if (!state.hasRequest && state.target == nullptr && state.opaqueSceneColorSnapshot == nullptr &&
        state.displayTarget == nullptr && state.antialiasedTarget == nullptr) {
        g_views.erase(found);
        return;
    }
    ReleaseSceneSourceLease(state.request.sceneSourceOverride);
    RetireViewResources(state);
    g_views.erase(found);
}

bool RenderPending(const SceneEnvironment& environment, const LIGHTING::SceneLightingSet& lighting,
                   uint64_t currentSceneRevision, ModelManager& modelManager, SkyManager& skyManager,
                   const AssetRegistry* assetRegistry,
                   const MATERIAL::MaterialExternalValueSnapshot* materialExternalValues,
                   const WORLD_SPRITES::WorldSpriteFrameSource* worldSpriteFrameSource,
                   const WORLD_TEXT::WorldTextFrameSource* worldTextFrameSource) {
    ScheduleViewCaptures([](RenderViewId id) -> std::optional<uint64_t> {
        const auto found = g_views.find(id.value);
        if (found == g_views.end() || !found->second->hasRequest || !found->second->request.visible)
            return {};
        auto& state = *found->second;
        if (!state.pending) {
            state.pendingSerial = g_requestSerial = NextNonZero(g_requestSerial);
            MarkOutputPending(state);
        }
        return state.output.outputRevision;
    });
    InteractiveViewState* selectedStatic = nullptr;
    InteractiveViewState* selectedRealtime = nullptr;
    uint32_t renderableViewCount = 0u;
    for (auto& [viewValue, state] : g_views) {
        (void)viewValue;
        if (!state->hasRequest) {
            continue;
        }
        if (!IsRenderableRequest(state->request, currentSceneRevision)) {
            SetOutputStatus(*state, state->request.visible ? EditorInteractiveViewStatus::Pending
                                                           : EditorInteractiveViewStatus::Idle);
            continue;
        }
        ++renderableViewCount;
        if (!state->pending && !state->request.continuousRealtime) {
            // A static view with a completed receipt does not consume a
            // frame until a new request arrives.
            continue;
        }

        if (state->request.continuousRealtime) {
            // Continuous views are round-robin candidates only after all
            // newly requested static views have had a chance to publish.
            // Keep the explicit realtime preference inside this class so
            // continuous views still win ties among themselves.
            const bool realtimePreferred = state->request.continuousRealtime && selectedRealtime == nullptr;
            const bool olderThanSelected =
                selectedRealtime != nullptr && state->lastRenderedSerial < selectedRealtime->lastRenderedSerial;
            const bool sameAgeHigherPriority =
                selectedRealtime != nullptr && state->lastRenderedSerial == selectedRealtime->lastRenderedSerial &&
                state->request.schedulingPriority > selectedRealtime->request.schedulingPriority;
            const bool sameAgeAndPriorityLowerId =
                selectedRealtime != nullptr && state->lastRenderedSerial == selectedRealtime->lastRenderedSerial &&
                state->request.schedulingPriority == selectedRealtime->request.schedulingPriority &&
                state->request.viewId.value < selectedRealtime->request.viewId.value;
            if (realtimePreferred || olderThanSelected || sameAgeHigherPriority || sameAgeAndPriorityLowerId) {
                selectedRealtime = state.get();
            }
            continue;
        }

        if (selectedStatic == nullptr) {
            selectedStatic = state.get();
            continue;
        }
        const bool olderPendingRequest =
            state->pendingSerial != 0u &&
            (selectedStatic->pendingSerial == 0u || state->pendingSerial < selectedStatic->pendingSerial);
        const bool samePendingRequestHigherPriority =
            state->pendingSerial == selectedStatic->pendingSerial &&
            state->request.schedulingPriority > selectedStatic->request.schedulingPriority;
        const bool samePendingRequestLowerId =
            state->pendingSerial == selectedStatic->pendingSerial &&
            state->request.schedulingPriority == selectedStatic->request.schedulingPriority &&
            state->request.viewId.value < selectedStatic->request.viewId.value;
        if (olderPendingRequest || samePendingRequestHigherPriority || samePendingRequestLowerId) {
            selectedStatic = state.get();
        }
    }
    g_schedulerStats.renderableViewCount = renderableViewCount;
    const bool staticHasFirstPublication = selectedStatic != nullptr && !selectedStatic->output.ready &&
                                           selectedStatic->output.status != EditorInteractiveViewStatus::Failed;
    const bool continuousBurstExhausted = g_continuousRenderStreak >= kMaximumContinuousRenderBurst;
    InteractiveViewState* selected =
        selectedStatic != nullptr &&
                (selectedRealtime == nullptr || staticHasFirstPublication || continuousBurstExhausted)
            ? selectedStatic
        : selectedRealtime != nullptr ? selectedRealtime
                                      : selectedStatic;
    const GFX::Context graphics = GFX::GetCurrentContext();
    if (selected == nullptr || graphics.cmdList == nullptr || graphics.device == nullptr) {
        return false;
    }
    ++g_scheduleSerial;
    if (g_scheduleSerial == 0u) {
        g_scheduleSerial = 1u;
    }
    selected->lastRenderedSerial = g_scheduleSerial;
    if (selected->request.continuousRealtime) {
        if (g_continuousRenderStreak < kMaximumContinuousRenderBurst) {
            ++g_continuousRenderStreak;
        }
    } else {
        g_continuousRenderStreak = 0u;
    }
    g_schedulerStats.scheduleSerial = g_scheduleSerial;
    g_schedulerStats.renderedViewId = selected->request.viewId.value;

    const uint32_t width = ClampExtent(selected->request.width);
    const uint32_t height = ClampExtent(selected->request.height);
    if (width == 0 || height == 0 || !EnsureTarget(*selected, width, height, selected->request.spatialAntiAliasing)) {
        FailOutput(*selected, EditorInteractiveViewFailure::RenderTargetUnavailable);
        return false;
    }
    const SceneEnvironment& effectiveEnvironment =
        selected->request.environmentOverride != nullptr ? *selected->request.environmentOverride : environment;
    const COLOR::ExposureFrameState viewExposure = ResolveViewExposureFrame(*selected, effectiveEnvironment);
    const COLOR::ScopedViewExposure viewExposureScope(viewExposure);
    const std::array<float, 4>& background = selected->request.backgroundColor;
    const bool uniformBackground = background[3] > 0.0f;
    std::array<float, 4> clearColor = ResolveClearColor(selected->request.shadingMode);
    if (uniformBackground) {
        // 表示参照の値を、この視点の前露出した HDR の値へ写す（×P/E）。
        const float displayToScene = COLOR::GetActiveViewExposure().z;
        clearColor = {background[0] * displayToScene, background[1] * displayToScene, background[2] * displayToScene,
                      1.0f};
    }
    selected->target->BeginCapture(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);

    RenderViewContext view{};
    view.viewId = selected->request.viewId;
    view.purpose = selected->request.purpose;
    view.cameraFrame = &selected->request.cameraFrame;
    view.assetRegistry = assetRegistry;
    // A workspace scene override owns the complete preview scene. Do not
    // borrow sprites/text from the active gameplay scene into that view.
    if (selected->request.sceneSourceOverride == nullptr) {
        view.worldSpriteFrameSource = worldSpriteFrameSource;
        view.worldTextFrameSource = worldTextFrameSource;
    }
    const PARTICLES::ParticleRenderFrameSource particleFrameSource{
        selected->request.particleInputOverride.get(),
        assetRegistry,
        materialExternalValues,
    };
    view.particleFrameSource = selected->request.particleInputOverride != nullptr ? &particleFrameSource : nullptr;
    const LIGHTING::SceneLightingSet& effectiveLighting =
        selected->request.lightingOverride != nullptr ? *selected->request.lightingOverride : lighting;
    const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& sceneSource = selected->request.sceneSourceOverride != nullptr
                                                                       ? *selected->request.sceneSourceOverride
                                                                       : MESHRENDERER::GetActiveGpuDrivenSceneSource();
    const EditorInteractiveStylizedPreparation stylizedPreparation = selected->stylizedView.Prepare(sceneSource);
    // An authored preview environment is an isolation boundary.  A null
    // override means "use the process-global runtime IBL", which leaked
    // whichever scene/probe set rendered previously into material views.
    // Bind an explicit empty set first, then replace it only with the
    // complete DefaultSky set requested by Studio Inspect.
    const IBL::IblEnvironmentSource emptyIblEnvironment{};
    // 中立照明は場面の物理的な空・IBL・探針を使わない（露出 1 の表示参照で描くので、昼の空の数千 nits が白く飛ぶ）。
    const bool neutralLighting = selected->request.shadingMode == EditorInteractiveShadingMode::Neutral ||
                                 selected->request.shadingMode == EditorInteractiveShadingMode::NeutralClay;
    const IBL::IblEnvironmentSource* iblEnvironmentOverride =
        selected->request.environmentOverride != nullptr || neutralLighting ? &emptyIblEnvironment : nullptr;
    if (selected->request.useDefaultSkyIbl) {
        const LIGHTING::DefaultSkyEnvironmentResult defaultSky =
            LIGHTING::EnsureDefaultSkyEnvironmentAvailable(skyManager);
        const IBL::IblEnvironmentSource& source = LIGHTING::GetDefaultSkyIblSource();
        if (defaultSky.iblLoaded && source.IsComplete()) {
            iblEnvironmentOverride = &source;
        }
    }
    SKYRENDERER::Render(GFX::GetCurrentContext(), selected->request.cameraFrame.camera, effectiveEnvironment,
                        modelManager, skyManager, !uniformBackground && !neutralLighting);
    if (!uniformBackground && !neutralLighting) {
        (void)PANORAMA_SKY::RenderPanoramaSkyLayer(GFX::GetCurrentContext(), selected->request.cameraFrame.camera,
                                                   effectiveEnvironment.sky, skyManager);
    }
    MESHRENDERER::EditorInteractiveRenderSettings renderSettings =
        ResolveRenderSettings(*selected, selected->request, stylizedPreparation);
    renderSettings.iblEnvironmentOverride = iblEnvironmentOverride;
    renderSettings.suppressSpatialLightingProviders |= neutralLighting;
    renderSettings.particleFrameSource = view.particleFrameSource;
    const bool hasToolGuides =
        selected->request.guideOverlay.showGroundGrid || selected->request.guideOverlay.showWorldAxes;
    GuidePassContext guidePassContext{
        &selected->request,
        width,
        height,
    };
    if (hasToolGuides) {
        renderSettings.depthAwareOverlayUserData = &guidePassContext;
        renderSettings.renderDepthAwareOverlay = &RenderDepthAwareGuidePass;
    }
    const MESHRENDERER::EditorInteractiveMeshRenderResult meshResult = MESHRENDERER::RenderEditorInteractiveOpaque(
        view, effectiveEnvironment, effectiveLighting, width, height, renderSettings);
    if (!hasToolGuides && selected->request.drawDebug) {
        RENDERER3D::DEBUG::RenderAllForView(selected->request.viewId, selected->request.cameraFrame.camera,
                                            static_cast<float>(width), static_cast<float>(height));
    }
    selected->target->EndCapture();

    if (meshResult != MESHRENDERER::EditorInteractiveMeshRenderResult::Ready) {
        (void)POST::PostSystem::RebindCurrentRenderTarget();
        FailOutput(*selected, ToViewFailure(meshResult));
        return false;
    }

    RenderTarget2D* displaySource = selected->target.get();
    if (!selected->stylizedView.Resolve(*selected->target, selected->targetGeneration, selected->request.viewId,
                                        selected->request.cameraFrame.camera, effectiveEnvironment, width, height,
                                        stylizedPreparation, displaySource)) {
        (void)POST::PostSystem::RebindCurrentRenderTarget();
        FailOutput(*selected, EditorInteractiveViewFailure::StylizedResolveFailed);
        return false;
    }

    // プレビューでも本編と同じ bloom を掛ける。この display 経路は
    // トーンマッピングしか持たないため、HDR のままここで後処理段を通し、
    // その結果をトーンマッピングへ渡す (本編と同じ順序)。
    if (displaySource != nullptr && effectiveEnvironment.bloom.enabled &&
        // QuadDrawer は display パイプラインが作る。先に用意しないと
        // 初回フレームで未初期化のまま参照してしまう。
        EnsureDisplayPipeline() && EnsurePreviewPostStage()) {
        g_previewPostStage->UpdateCommonParams(0.0f, static_cast<int>(width), static_cast<int>(height));
        g_previewPostStage->SetExposureFrame(viewExposure);
        // HDR lighting と display が参照する同じ view 環境を使う。
        BloomSettings previewBloom{};
        previewBloom.enabled = effectiveEnvironment.bloom.enabled;
        previewBloom.threshold = effectiveEnvironment.bloom.threshold;
        previewBloom.intensity = effectiveEnvironment.bloom.intensity;
        previewBloom.radius = effectiveEnvironment.bloom.radius;
        previewBloom.downsampleCount = effectiveEnvironment.bloom.downsampleCount;
        g_previewPostStage->SetBloomSettings(previewBloom);
        // ツールプレビューは bloom 専用の入口を使う。ResolveHdr は画面衝撃・
        // 自動露出計測・サングレア・遷移などゲーム状態に紐づく段を含み、
        // 静止中のプレビューでもフレーム毎に結果が揺れる (ちらつきの原因)。
        RenderTarget2D* bloomed = g_previewPostStage->ResolveHdrBloomOnly(*displaySource, *g_displayQuad);
        if (bloomed != nullptr && bloomed != displaySource) {
            displaySource = bloomed;
        }
    }

    if (displaySource == nullptr ||
        !ResolveDisplayTarget(*selected, effectiveEnvironment, viewExposure, *displaySource)) {
        (void)POST::PostSystem::RebindCurrentRenderTarget();
        FailOutput(*selected, EditorInteractiveViewFailure::DisplayResolveFailed);
        return false;
    }

    RenderTarget2D* publishedTarget = selected->displayTarget.get();
    if (ResolveSpatialAntiAliasing(*selected, *selected->displayTarget)) {
        publishedTarget = selected->antialiasedTarget.get();
    }
    const D3D12_GPU_DESCRIPTOR_HANDLE colorSrv =
        publishedTarget != nullptr ? PublishOutputSrv(*selected, *publishedTarget) : D3D12_GPU_DESCRIPTOR_HANDLE{};
    (void)POST::PostSystem::RebindCurrentRenderTarget();

    if (colorSrv.ptr == 0) {
        FailOutput(*selected, EditorInteractiveViewFailure::OutputDescriptorUnavailable);
        return false;
    }

    selected->output.viewId = selected->request.viewId;
    selected->output.colorSrv = colorSrv;
    selected->output.width = width;
    selected->output.height = height;
    selected->output.sceneRevision =
        selected->request.sceneSourceOverride != nullptr ? selected->request.sceneRevision : currentSceneRevision;
    selected->output.requestedRevision = selected->request.sceneRevision;
    ++selected->output.outputRevision;
    if (selected->output.outputRevision == 0) {
        selected->output.outputRevision = 1;
    }
    selected->output.ready = true;
    selected->output.status = EditorInteractiveViewStatus::Ready;
    selected->output.failure = EditorInteractiveViewFailure::None;
    selected->pending = false;
    selected->pendingSerial = 0u;
    RecordViewCapture(selected->request.viewId, true, selected->output.outputRevision, publishedTarget->GetResource(),
                      publishedTarget->GetColorState(), width, height, &selected->request.cameraFrame,
                      selected->output.sceneRevision);
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

std::string_view GetEditorInteractiveViewFailureMessage(EditorInteractiveViewFailure failure) noexcept {
    switch (failure) {
    case EditorInteractiveViewFailure::RenderTargetUnavailable: return "Preview failed: render target unavailable";
    case EditorInteractiveViewFailure::MeshRendererUnavailable: return "Preview failed: mesh renderer unavailable";
    case EditorInteractiveViewFailure::MaterialFrameUnavailable: return "Preview waiting: material frame unavailable";
    case EditorInteractiveViewFailure::FrameResourcesUnavailable: return "Preview failed: frame resources unavailable";
    case EditorInteractiveViewFailure::SceneResidencyUnavailable:
        return "Preview waiting: model GPU residency incomplete";
    case EditorInteractiveViewFailure::StylizedSurfacePassFailed: return "Preview failed: NPR surface-mask pass";
    case EditorInteractiveViewFailure::ForwardPassFailed:
        return "Preview failed: no executable forward material partition";
    case EditorInteractiveViewFailure::StylizedGeometryPassFailed: return "Preview failed: outline geometry subpass";
    case EditorInteractiveViewFailure::StylizedResolveFailed: return "Preview failed: NPR / outline resolve";
    case EditorInteractiveViewFailure::DisplayResolveFailed: return "Preview failed: display resolve";
    case EditorInteractiveViewFailure::OutputDescriptorUnavailable:
        return "Preview failed: output descriptor unavailable";
    case EditorInteractiveViewFailure::None:
    default: return "Preview failed: unknown renderer error";
    }
}

EditorInteractiveSchedulerStats GetSchedulerStats() { return g_schedulerStats; }

void Shutdown() {
    ShutdownViewCaptures();
    RENDERER3D::DEBUG::ShutdownEditorViewResources();
    for (auto& [viewValue, state] : g_views) {
        (void)viewValue;
        ReleaseSceneSourceLease(state->request.sceneSourceOverride);
        RetireViewResources(*state);
    }
    g_sceneSourceLeaseCounts.clear();
    MESHRENDERER::ShutdownEditorInteractiveResources();
    g_views.clear();
    g_fxaaEffect.reset();
    g_displayEffect.reset();
    g_previewPostStage.reset();
    g_displayQuad.reset();
    ShutdownEditorInteractiveStylizedSharedResources();
    g_scheduleSerial = 0u;
    g_requestSerial = 0u;
    g_continuousRenderStreak = 0u;
    g_schedulerStats = {};
}

} // namespace HIKARI::RENDER3D::EDITORVIEW

#endif
