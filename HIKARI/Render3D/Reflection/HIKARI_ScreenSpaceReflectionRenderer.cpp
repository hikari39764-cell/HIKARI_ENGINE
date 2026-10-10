#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Render3D/Reflection/HIKARI_ScreenSpaceReflectionRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <d3dx12.h>
#include "Assets/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"
#include "Render3D/Color/HIKARI_ViewExposure.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"
#include "Render3D/RayTracing/HIKARI_RuntimeRayQueryBindings.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"

namespace HIKARI::RENDER3D::REFLECTION {
namespace {
using Microsoft::WRL::ComPtr;
constexpr uint32_t kResidentViews = 2;
constexpr uint64_t kViewTargetBudget = 32ull * 1024 * 1024;
constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr auto kReadState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
struct alignas(16) Constants {
    MATH::Mat4 viewProj{}, invViewProj{};
    MATH::Vec4 cameraPosition{}, screenParams{}, traceScreenParams{};
    MATH::Vec4 traceParams{}, resolveParams{}, frameParams{};
    MATH::Mat4 previousInvViewProj{};
    MATH::Vec4 previousCameraPosition{}, jitterPixels{};
};
static_assert(sizeof(Constants) == 320);
// 光線追跡の反射 pass の定数。SSR の trace 目標へ直接書く（UAV）。画面外の当たった点は動的 GI の volume で照らす。
struct alignas(16) RayConstants {
    MATH::Mat4 viewProj{}, invViewProj{};
    MATH::Vec4 cameraPosition{}, screenParams{}, traceScreenParams{}, rayParams{}, viewExposure{};
    LIGHTPROBE::IrradianceVolumeGpuData giVolumes[LIGHTPROBE::kDynamicGiVolumeCount]{};
    RAYTRACING::RuntimeRayLightingGpuData lighting{};
};
static_assert(sizeof(RayConstants) == 608);
constexpr const wchar_t* kRayShaderPath = L"HIKARI/Shaders/Post_RayTracedReflectionsCS.hlsl";

float FiniteClamp(float value, float lower, float upper, float fallback) {
    return std::isfinite(value) ? std::clamp(value, lower, upper) : fallback;
}

// SSR の半解像度 trace と検証用の履歴のみ。Scene の depth/HZB/Motion は借りる。
struct Target {
    ComPtr<ID3D12Resource> resource;
    RenderResourceView srv;
    RenderResourceView uav; // trace だけ。光線追跡の反射が書く。
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    D3D12_RESOURCE_STATES state = kReadState;
    uint64_t allocatedBytes = 0;
    void Reset(const GFX::Context& graphics, bool completed = false) {
        if (completed) {
            ReleaseRenderResourceDescriptor(srv);
            srv = {};
            if (uav.IsValid())
                ReleaseRenderResourceDescriptor(uav);
            uav = {};
            resource.Reset();
        } else {
            RetireRenderResourceDescriptor(srv, graphics, "SSR.SRV");
            if (uav.IsValid())
                RetireRenderResourceDescriptor(uav, graphics, "SSR.UAV");
            GFX::RetireD3D12ObjectForFrame(resource, graphics, "SSR.Target");
        }
        rtv = {};
        state = kReadState;
        allocatedBytes = 0;
    }
    void Transition(ID3D12GraphicsCommandList* commands, D3D12_RESOURCE_STATES next) {
        if (state == next)
            return;
        const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource.Get(), state, next);
        commands->ResourceBarrier(1, &barrier);
        state = next;
    }
};
struct ViewResources {
    TEMPORAL::TemporalViewLease lease{};
    uint64_t lastFrame = 0, settingsKey = 0, useSerial = 0, lastRetireFence = 0;
    uint32_t width = 0, height = 0, writeIndex = 0, targetCount = 0;
    uint32_t sourceWidth = 0, sourceHeight = 0;
    bool valid = false;
    // trace, radiance[2], validation[2] (oct-normal, roughness, linear distance)
    std::array<Target, 5> targets;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    void Reset(const GFX::Context& graphics, bool completed = false) {
        for (auto& target : targets)
            target.Reset(graphics, completed);
        if (completed)
            rtvHeap.Reset();
        else
            GFX::RetireD3D12ObjectForFrame(rtvHeap, graphics, "SSR.RTVHeap");
        lease = {};
        lastFrame = settingsKey = useSerial = lastRetireFence = 0;
        width = height = sourceWidth = sourceHeight = writeIndex = targetCount = 0;
        valid = false;
    }
    bool Ensure(const GFX::Context& graphics, uint32_t requestedWidth, uint32_t requestedHeight, bool history,
                TEMPORAL::TemporalViewLease requestedLease, bool& deferred) {
        const uint32_t count = history ? 5u : 1u;
        if (sourceWidth == requestedWidth && sourceHeight == requestedHeight && targetCount == count &&
            lease.Matches(requestedLease))
            return true;
        const auto completed = graphics.frameFence->GetCompletedValue();
        if (completed == UINT64_MAX || completed < lastRetireFence) {
            deferred = true;
            return false;
        }
        // GPU が使う旧 target を積み増さない。未完了なら今 frame は既存 reflection fallback を使う。
        Reset(graphics, true);
        uint32_t resolvedWidth = requestedWidth, resolvedHeight = requestedHeight;
        auto desc = CD3DX12_RESOURCE_DESC::Tex2D(kFormat, resolvedWidth, resolvedHeight, 1, 1, 1, 0,
                                                 D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        uint64_t allocationBytes = 0;
        for (;;) {
            allocationBytes = graphics.device->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
            if (!allocationBytes || allocationBytes == UINT64_MAX)
                return false;
            if (allocationBytes <= kViewTargetBudget / count)
                break;
            if (resolvedWidth == 1 && resolvedHeight == 1)
                return false;
            // アラインメントを含む実 allocation で判定し、縦横比を保って縮小する。
            const double scale = std::min(0.95, std::sqrt(double(kViewTargetBudget / count) / allocationBytes));
            resolvedWidth = std::max(1u, uint32_t(resolvedWidth * scale));
            resolvedHeight = std::max(1u, uint32_t(resolvedHeight * scale));
            desc.Width = resolvedWidth;
            desc.Height = resolvedHeight;
        }
        const D3D12_DESCRIPTOR_HEAP_DESC heapDesc{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, count};
        if (FAILED(graphics.device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeap))))
            return false;
        const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
        const D3D12_CLEAR_VALUE clear{kFormat, {0, 0, 0, 0}};
        const UINT stride = graphics.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        for (uint32_t i = 0; i < count; ++i) {
            auto& target = targets[i];
            auto targetDesc = desc;
            if (i == 0)
                targetDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            if (FAILED(graphics.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &targetDesc, kReadState,
                                                                &clear, IID_PPV_ARGS(&target.resource)))) {
                Reset(graphics, true);
                return false;
            }
            if (i == 0) {
                target.uav = AllocateTexture2DUavDescriptor(target.resource.Get(), kFormat);
                if (!target.uav.IsValid()) {
                    Reset(graphics, true);
                    return false;
                }
            }
            target.srv = AllocateRenderResourceDescriptor();
            if (!target.srv.IsValid()) {
                Reset(graphics, true);
                return false;
            }
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = kFormat;
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture2D.MipLevels = 1;
            graphics.device->CreateShaderResourceView(target.resource.Get(), &srv, target.srv.cpu);
            target.rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
            target.rtv.ptr += i * stride;
            graphics.device->CreateRenderTargetView(target.resource.Get(), nullptr, target.rtv);
            target.resource->SetName(i == 0  ? L"HIKARI.SSR.Trace"
                                     : i < 3 ? L"HIKARI.SSR.HistoryRadiance"
                                             : L"HIKARI.SSR.HistoryValidation");
            target.allocatedBytes = allocationBytes;
        }
        sourceWidth = requestedWidth;
        sourceHeight = requestedHeight;
        width = resolvedWidth;
        height = resolvedHeight;
        targetCount = count;
        lease = requestedLease;
        return true;
    }
};
} // namespace

struct ScreenSpaceReflectionRenderer::State {
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> tracePipeline, historyPipeline, resolvePipeline;
    ComPtr<ID3D12RootSignature> rayRoot;
    ComPtr<ID3D12PipelineState> rayPipeline;
    bool rayPipelineFailed = false;
    std::array<DX::DynamicUploadBuffer, GFX::kFrameResourceCount> uploads;
    std::array<ViewResources, kResidentViews> views;
    GFX::Context context{};
    uint64_t useSerial = 0, pipelineRetryFrame = 0;
    // 光線追跡の反射：動的 GI と同じ場面・材質・光源の束ね方に、深度・面・画面の色・trace の UAV（space9）を足す。
    bool EnsureRayPipeline(const GFX::Context& graphics) {
        if (rayRoot && rayPipeline)
            return true;
        if (rayPipelineFailed || !GFX::SupportsShaderModel6(graphics.device))
            return false;
        rayPipelineFailed = true;
        // 共通の 0..9 + 空(10) に、深度(11)・面(12)・画面の色(13)・trace の UAV(14) を続ける。
        constexpr UINT kSky = RAYTRACING::kRuntimeRayQueryRootParameterCount;
        std::array<CD3DX12_ROOT_PARAMETER, kSky + 5u> parameters;
        std::array<CD3DX12_DESCRIPTOR_RANGE, RAYTRACING::kRuntimeRayQueryRangeCount + 4u> ranges;
        RAYTRACING::InitRuntimeRayQueryRootParameters(parameters, ranges, kSky);
        ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 9);
        ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 9);
        ranges[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 9);
        ranges[6].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 9);
        for (UINT i = 0; i < 4u; ++i)
            parameters[kSky + 1u + i].InitAsDescriptorTable(1, &ranges[3u + i]);
        const auto common = RAYTRACING::RuntimeRayQueryStaticSamplers();
        std::array<CD3DX12_STATIC_SAMPLER_DESC, 3> samplers{common[0], common[1], {}};
        samplers[2].Init(2, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                         D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        const CD3DX12_ROOT_SIGNATURE_DESC rootDesc(UINT(parameters.size()), parameters.data(), UINT(samplers.size()),
                                                   samplers.data());
        if (!HIKARI_DX_CHECK(GFX::CreateRootSignature(graphics.device, rootDesc, &rayRoot), "SSR ray root creation"))
            return false;
        ComPtr<ID3DBlob> shader;
        if (!GFX::CompileShaderFileSm6(kRayShaderPath, "CSMain", L"cs_6_5", shader.GetAddressOf())) {
            DEBUGLOG::PushRenderError(
                "[SSR][RayTracing] compute shader compile failed; environment fallback retained.");
            return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = rayRoot.Get();
        desc.CS = {shader->GetBufferPointer(), shader->GetBufferSize()};
        if (!HIKARI_DX_CHECK(graphics.device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&rayPipeline)),
                             "SSR ray pipeline creation"))
            return false;
        rayPipelineFailed = false;
        return true;
    }

    bool EnsurePipeline(const GFX::Context& graphics) {
        if (root && tracePipeline && historyPipeline && resolvePipeline)
            return true;
        std::array<CD3DX12_DESCRIPTOR_RANGE, 11> ranges;
        std::array<CD3DX12_ROOT_PARAMETER, 12> parameters;
        parameters[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
        for (UINT i = 0; i < ranges.size(); ++i) {
            ranges[i].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, i);
            parameters[i + 1].InitAsDescriptorTable(1, &ranges[i], D3D12_SHADER_VISIBILITY_PIXEL);
        }
        std::array<CD3DX12_STATIC_SAMPLER_DESC, 2> samplers;
        for (UINT i = 0; i < samplers.size(); ++i)
            samplers[i].Init(i, i == 0 ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT,
                             D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                             D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        const CD3DX12_ROOT_SIGNATURE_DESC rootDesc(UINT(parameters.size()), parameters.data(), UINT(samplers.size()),
                                                   samplers.data());
        if (!HIKARI_DX_CHECK(GFX::CreateRootSignature(graphics.device, rootDesc, &root), "SSR root creation"))
            return false;
        namespace SHADER = ASSETS::SHADER;
        SHADER::ShaderAssetContractExpectation contract{};
        contract.engineInterfaceId = "HIKARI.ScreenSpaceReflection";
        contract.engineInterfaceVersion = 1;
        contract.rootLayoutId = "HIKARI.ScreenSpaceReflection.Root";
        contract.rootLayoutVersion = 1;
        if (!HASH::ComputeSha256(
                "SSR:320;scene-linear radiance/confidence;half-resolution validated history;unjittered motion pixels;metadata RG32F",
                contract.engineInterfaceHash) ||
            !HASH::ComputeSha256("CBV(b0);SRV(t0..t10,individual);Sampler(s0,linearClamp);Sampler(s1,pointClamp)",
                                 contract.rootLayoutHash))
            return false;
        const auto caps = GFX::QueryShaderDeviceCapabilities(graphics.device);
        contract.availableDeviceCaps = {caps.shaderModel, caps.rootSignatureVersion, caps.featureMask};
        const auto load = [&](const char* entry, SHADER::ShaderStage stage) {
            auto result = SHADER::ResolveBuiltinShaderEntry(
                "HIKARI.EngineShader.Post_ScreenSpaceReflections",
                {entry, stage, stage == SHADER::ShaderStage::Vertex ? "vs_6_0" : "ps_6_0"}, contract);
            if (!result)
                DEBUGLOG::PushRenderError("SSR cooked shader unavailable: " + result.diagnostic);
            return result;
        };
        const auto vertex = load("VSMain", SHADER::ShaderStage::Vertex);
        if (!vertex)
            return false;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = root.Get();
        const auto vs = vertex.lease.GetDxil();
        desc.VS = {vs.data(), vs.size()};
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.SampleMask = UINT_MAX;
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.SampleDesc.Count = 1;
        desc.RTVFormats[0] = kFormat;
        for (UINT i = 0; i < 3; ++i) {
            const auto pixel = load(i == 0   ? "PSTrace"
                                    : i == 1 ? "PSHistory"
                                             : "PSResolve",
                                    SHADER::ShaderStage::Pixel);
            if (!pixel)
                return false;
            const auto ps = pixel.lease.GetDxil();
            desc.PS = {ps.data(), ps.size()};
            desc.NumRenderTargets = i == 1 ? 2u : 1u;
            desc.RTVFormats[1] = i == 1 ? kFormat : DXGI_FORMAT_UNKNOWN;
            auto& pipeline = i == 0 ? tracePipeline : i == 1 ? historyPipeline : resolvePipeline;
            if (!HIKARI_DX_CHECK(graphics.device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline)),
                                 "SSR pipeline creation"))
                return false;
        }
        return true;
    }
};

ScreenSpaceReflectionRenderer::ScreenSpaceReflectionRenderer() : state_(std::make_unique<State>()) {}
ScreenSpaceReflectionRenderer::~ScreenSpaceReflectionRenderer() = default;

bool ScreenSpaceReflectionRenderer::Render(const GFX::Context& graphics,
                                           const PIPELINE::RenderTargetAccess& targetAccess,
                                           const SURFACE::SceneSurfaceSet& surfaces,
                                           const MESHRENDERER::CameraCB& camera,
                                           const ScreenSpaceReflectionSettings& settings, RenderDebugView debugView,
                                           const TEMPORAL::TemporalFrameState& temporal,
                                           const RAYTRACING::RuntimeRayLightingFrame* rayLighting,
                                           const MESHRENDERER::MeshMaterialGpuFrameView* materials) {
    debugState_ = {};
    debugState_.requested = settings.enabled && settings.quality != ScreenSpaceReflectionQuality::Off;
    using Semantic = GRAPH::RenderGraphSemantic;
    using Phase = GRAPH::RenderGraphSemanticPhase;
    const auto* color = surfaces.TryGetTexture({Semantic::SceneColorOpaque, Phase::Opaque});
    const auto* depth = surfaces.TryGetTexture({Semantic::SceneDepth, Phase::Imported});
    const auto* surface = surfaces.TryGetTexture({Semantic::PackedSurface, Phase::PostOpaque});
    const auto* hzb = surfaces.TryGetDepthPyramid({Semantic::CurrentDepthPyramid, Phase::PostOpaque});
    const auto* fallback = surfaces.TryGetTexture({Semantic::ReflectionFallbackSpecular, Phase::Opaque});
    const auto* response = surfaces.TryGetTexture({Semantic::ReflectionSpecularResponse, Phase::Opaque});
    const auto* motion = surfaces.TryGetTexture({Semantic::MotionVectors, Phase::Temporal});
    const auto* metadata = surfaces.TryGetTexture({Semantic::MotionMetadata, Phase::Temporal});
    debugState_.sceneSurfaceReady = color && depth && surface && fallback && response;
    debugState_.currentHzbReady = surfaces.HasCurrentDepthHierarchy();
    auto& state = *state_;
    if (!debugState_.requested || !debugState_.sceneSurfaceReady || !debugState_.currentHzbReady || !hzb ||
        !graphics.device || !graphics.cmdList || !graphics.srvHeap || !graphics.deviceGeneration ||
        !graphics.frameFence || !graphics.currentFrameRetireFenceValue ||
        graphics.frameIndex >= GFX::kFrameResourceCount || !temporal.view.IsValid() ||
        !surfaces.MatchesFrame(temporal.publication) || surfaces.GetDeviceGeneration() != graphics.deviceGeneration)
        return false;
    for (const auto* input : {color, surface, fallback, response})
        if (input->width != depth->width || input->height != depth->height)
            return false;
    // 別 device/heap の履歴や PSO は使わない。既存 shutdown が Release を所有する。
    if (state.context.device &&
        (state.context.device != graphics.device || state.context.deviceGeneration != graphics.deviceGeneration ||
         state.context.srvHeap != graphics.srvHeap || state.context.frameFence != graphics.frameFence))
        return false;
    state.context = graphics;
    UpdateRenderResourceDescriptorPoolContext(graphics);
    const auto pool = GetRenderResourceDescriptorPoolStats();
    if (!pool.contextAvailable || !pool.contextCompatible || temporal.frameIndex < state.pipelineRetryFrame)
        return false;
    if (!state.EnsurePipeline(graphics)) {
        state.pipelineRetryFrame = temporal.frameIndex + 60;
        return false;
    }
    debugState_.pipelineReady = true;
    debugState_.hzbMipCount = hzb->mipCount;
    debugState_.maxSteps = ResolveScreenSpaceReflectionMaxSteps(settings.quality);
    debugState_.traceWidth = depth->width / 2u + depth->width % 2u;
    debugState_.traceHeight = depth->height / 2u + depth->height % 2u;
    debugState_.maxDistance = FiniteClamp(settings.maxDistance, 1, 200, 30);
    debugState_.maxRoughness = FiniteClamp(settings.maxRoughness, 0.05f, 1, 0.8f);
    const bool useHistory = settings.quality != ScreenSpaceReflectionQuality::Low &&
                            debugView != RenderDebugView::SsrHit && debugView != RenderDebugView::SsrConfidence;
    auto* view = &state.views[0];
    for (auto& candidate : state.views) {
        if (candidate.lease.Matches(temporal.view)) {
            view = &candidate;
            break;
        }
        if (candidate.useSerial < view->useSerial)
            view = &candidate;
    }
    if (view->lease.Matches(temporal.view) && view->useSerial != 0 && view->lastFrame == temporal.frameIndex)
        return false;
    const bool ready = view->Ensure(graphics, debugState_.traceWidth, debugState_.traceHeight, useHistory,
                                    temporal.view, debugState_.resizeDeferred);
    for (const auto& resident : state.views) {
        debugState_.residentViews += resident.targetCount != 0;
        for (const auto& target : resident.targets)
            debugState_.workingSetBytes += target.allocatedBytes;
    }
    if (!ready)
        return false;
    debugState_.resolutionLimited = view->width != debugState_.traceWidth || view->height != debugState_.traceHeight;
    debugState_.traceWidth = view->width;
    debugState_.traceHeight = view->height;
    view->useSerial = ++state.useSerial;
    Constants constants{};
    constants.viewProj = camera.viewProj;
    constants.invViewProj = camera.invViewProj;
    constants.cameraPosition = camera.cameraPos;
    constants.screenParams = {float(depth->width), float(depth->height), 1.0f / depth->width, 1.0f / depth->height};
    constants.traceScreenParams = {float(view->width), float(view->height), 1.0f / view->width, 1.0f / view->height};
    constants.traceParams = {debugState_.maxDistance, FiniteClamp(settings.thickness, 0.0001f, 0.05f, 0.002f),
                             FiniteClamp(settings.intensity, 0, 2, 1), debugState_.maxRoughness};
    constants.resolveParams = {FiniteClamp(settings.stride, 0.02f, 2, 0.1f), float(debugState_.maxSteps),
                               float(std::max(1u, hzb->mipCount)), FiniteClamp(settings.edgeFade, 0.01f, 0.35f, 0.1f)};
    uint64_t key = HASH::Fnv1aBytes(HASH::kFnv1a64OffsetBasis, &constants.traceParams, sizeof(constants.traceParams));
    key = HASH::Fnv1aBytes(key, &constants.resolveParams, sizeof(constants.resolveParams));
    const bool motionReady = motion && metadata && motion->width == depth->width && motion->height == depth->height &&
                             metadata->width == depth->width && metadata->height == depth->height;
    debugState_.historyUsed = useHistory && view->valid && temporal.historyValid && !temporal.resetHistory &&
                              temporal.camera.previousValid && view->lastFrame + 1 == temporal.frameIndex &&
                              view->settingsKey == key && motionReady;
    constants.frameParams = {debugView == RenderDebugView::SsrConfidence ? 1.0f
                             : debugView == RenderDebugView::SsrHit      ? 2.0f
                                                                         : 0.0f,
                             RENDER3D::COLOR::GetActiveViewExposure().w, debugState_.historyUsed ? 1.0f : 0.0f, 0.95f};
    constants.previousInvViewProj = MATH::Inverse(temporal.camera.prevViewProj);
    constants.previousCameraPosition = temporal.camera.prevCameraPos;
    constants.jitterPixels = {temporal.camera.jitter.x, temporal.camera.jitter.y, temporal.camera.prevJitter.x,
                              temporal.camera.prevJitter.y};
    // 失敗した frame の履歴は公開しない。CB は view ごとに immutable な upload slice を持つ。
    view->valid = false;
    auto& upload = state.uploads[graphics.frameIndex];
    if (!upload.IsInitialized() && !upload.Init(graphics.device, 4096))
        return false;
    upload.ResetForFrame(graphics.currentFrameRetireFenceValue);
    const auto slice = upload.Allocate(sizeof(Constants));
    if (!slice)
        return false;
    std::memcpy(slice.cpuAddress, &constants, sizeof(constants));
    if (!targetAccess.BeginDepthRead())
        return false;
    // composite が失敗しても trace/history の GPU 使用と同一 frame の再入を記録する。
    view->lastRetireFence = graphics.currentFrameRetireFenceValue;
    view->lastFrame = temporal.frameIndex;
    auto* commands = graphics.cmdList;
    GFX::PIX::ScopedGpuEvent event(commands, GFX::PIX::kColorPost, "ScreenSpaceReflections");
    GFX::GPU_PROFILE::ScopedGpuTimer timer(commands, GFX::GPU_PROFILE::Pass::ScreenSpaceReflections);
    auto& trace = view->targets[0];
    const auto bind = [&](D3D12_GPU_DESCRIPTOR_HANDLE traceSrv) {
        ID3D12DescriptorHeap* heaps[]{graphics.srvHeap};
        commands->SetDescriptorHeaps(1, heaps);
        commands->SetGraphicsRootSignature(state.root.Get());
        commands->SetGraphicsRootConstantBufferView(0, slice.gpuAddress);
        const auto& history = view->targets[1 + (1 - view->writeIndex)];
        const auto& validation = view->targets[3 + (1 - view->writeIndex)];
        const std::array srvs{color->srv,
                              depth->srv,
                              surface->srv,
                              hzb->pyramidSrv,
                              traceSrv,
                              fallback->srv,
                              response->srv,
                              useHistory ? history.srv.gpu : trace.srv.gpu,
                              useHistory ? validation.srv.gpu : trace.srv.gpu,
                              motionReady ? motion->srv : color->srv,
                              motionReady ? metadata->srv : color->srv};
        for (UINT i = 0; i < srvs.size(); ++i)
            commands->SetGraphicsRootDescriptorTable(i + 1, srvs[i]);
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands->IASetVertexBuffers(0, 0, nullptr);
        commands->IASetIndexBuffer(nullptr);
    };
    const D3D12_VIEWPORT viewport{0, 0, float(view->width), float(view->height), 0, 1};
    const D3D12_RECT scissor{0, 0, LONG(view->width), LONG(view->height)};
    commands->RSSetViewports(1, &viewport);
    commands->RSSetScissorRects(1, &scissor);
    bind(trace.srv.gpu);
    // 光線追跡が使える時は SSR の trace の代わりに全 texel を光線追跡の反射で書く。SSR と光線追跡で向きと当たる形状が
    // 食い違うと、両者の境目と確信度の段差が床に縞や輪として出る。加速構造に入っていない面は映らないだけなので使う。
    const bool rayCapable =
        rayLighting && materials && *materials && rayLighting->status == RAYTRACING::RuntimeRayLightingStatus::Ready &&
        rayLighting->scene.ready && !rayLighting->scene.empty && rayLighting->scene.accelerationStructure != 0 &&
        debugView == RenderDebugView::None && trace.uav.IsValid() && state.EnsureRayPipeline(graphics);
    const auto raySlice = rayCapable ? upload.Allocate(sizeof(RayConstants)) : DX::UploadAllocation{};
    if (raySlice) {
        RayConstants rayConstants{};
        rayConstants.viewProj = camera.viewProj;
        rayConstants.invViewProj = camera.invViewProj;
        rayConstants.cameraPosition = camera.cameraPos;
        rayConstants.screenParams = constants.screenParams;
        rayConstants.traceScreenParams = constants.traceScreenParams;
        rayConstants.rayParams = {debugState_.maxDistance, debugState_.maxRoughness, float(temporal.frameIndex % 1024u),
                                  0.0f};
        rayConstants.viewExposure = RENDER3D::COLOR::GetActiveViewExposure();
        const auto giVolumes = LIGHTPROBE::GetDynamicGiVolumes();
        for (size_t index = 0; index < giVolumes.size() && index < std::size(rayConstants.giVolumes); ++index)
            if (giVolumes[index].valid)
                rayConstants.giVolumes[index] = LIGHTPROBE::BuildIrradianceVolumeGpuData(giVolumes[index], 1.0f);
        rayConstants.lighting = rayLighting->data;
        std::memcpy(raySlice.cpuAddress, &rayConstants, sizeof(rayConstants));
        GFX::PIX::ScopedGpuEvent rayEvent(commands, GFX::PIX::kColorPost, "ScreenSpaceReflections.RayTraced");
        // 画面の色と面の記述は pixel shader 用の状態で渡されるので、compute からも読める状態を足して戻す。
        std::array<D3D12_RESOURCE_BARRIER, 2> toCompute{};
        UINT inputBarriers = 0;
        for (const auto* input : {color, surface})
            if (input->resource && (input->state & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) == 0)
                toCompute[inputBarriers++] = CD3DX12_RESOURCE_BARRIER::Transition(
                    input->resource, input->state, input->state | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (inputBarriers)
            commands->ResourceBarrier(inputBarriers, toCompute.data());
        trace.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commands->SetComputeRootSignature(state.rayRoot.Get());
        commands->SetPipelineState(state.rayPipeline.Get());
        commands->SetComputeRootConstantBufferView(0, raySlice.gpuAddress);
        constexpr UINT kSky = RAYTRACING::kRuntimeRayQueryRootParameterCount;
        RAYTRACING::BindRuntimeRayQueryRoots(commands, graphics, *rayLighting, *materials, kSky);
        commands->SetComputeRootDescriptorTable(kSky + 1u, depth->srv);
        commands->SetComputeRootDescriptorTable(kSky + 2u, surface->srv);
        commands->SetComputeRootDescriptorTable(kSky + 3u, color->srv);
        commands->SetComputeRootDescriptorTable(kSky + 4u, trace.uav.gpu);
        commands->Dispatch((view->width + 7u) / 8u, (view->height + 7u) / 8u, 1u);
        trace.Transition(commands, kReadState);
        for (UINT i = 0; i < inputBarriers; ++i)
            std::swap(toCompute[i].Transition.StateBefore, toCompute[i].Transition.StateAfter);
        if (inputBarriers)
            commands->ResourceBarrier(inputBarriers, toCompute.data());
    } else {
        trace.Transition(commands, D3D12_RESOURCE_STATE_RENDER_TARGET);
        commands->OMSetRenderTargets(1, &trace.rtv, FALSE, nullptr);
        commands->SetPipelineState(state.tracePipeline.Get());
        commands->DrawInstanced(3, 1, 0, 0);
        trace.Transition(commands, kReadState);
    }
    auto resolvedTrace = trace.srv.gpu;
    if (useHistory) {
        auto& history = view->targets[1 + view->writeIndex];
        auto& validation = view->targets[3 + view->writeIndex];
        history.Transition(commands, D3D12_RESOURCE_STATE_RENDER_TARGET);
        validation.Transition(commands, D3D12_RESOURCE_STATE_RENDER_TARGET);
        const std::array rtvs{history.rtv, validation.rtv};
        commands->OMSetRenderTargets(2, rtvs.data(), FALSE, nullptr);
        commands->SetPipelineState(state.historyPipeline.Get());
        commands->DrawInstanced(3, 1, 0, 0);
        history.Transition(commands, kReadState);
        validation.Transition(commands, kReadState);
        resolvedTrace = history.srv.gpu;
    }
    if (!targetAccess.BeginDepthRead()) {
        targetAccess.EndDepthRead();
        return false;
    }
    bind(resolvedTrace);
    commands->SetPipelineState(state.resolvePipeline.Get());
    commands->DrawInstanced(3, 1, 0, 0);
    targetAccess.EndDepthRead();
    view->valid = useHistory && motionReady;
    view->lastFrame = temporal.frameIndex;
    view->settingsKey = key;
    view->writeIndex = 1 - view->writeIndex;
    debugState_.rendered = true;
    return true;
}

void ScreenSpaceReflectionRenderer::Release() {
    auto& state = *state_;
    for (auto& view : state.views)
        view.Reset(state.context);
    for (auto& upload : state.uploads)
        upload.Finalize(&state.context);
    GFX::RetireD3D12ObjectForFrame(state.tracePipeline, state.context, "SSR.TracePSO");
    GFX::RetireD3D12ObjectForFrame(state.historyPipeline, state.context, "SSR.HistoryPSO");
    GFX::RetireD3D12ObjectForFrame(state.resolvePipeline, state.context, "SSR.ResolvePSO");
    GFX::RetireD3D12ObjectForFrame(state.root, state.context, "SSR.Root");
    GFX::RetireD3D12ObjectForFrame(state.rayPipeline, state.context, "SSR.RayPSO");
    GFX::RetireD3D12ObjectForFrame(state.rayRoot, state.context, "SSR.RayRoot");
    state.rayPipelineFailed = false;
    state.context = {};
    state.useSerial = state.pipelineRetryFrame = 0;
    debugState_ = {};
}
} // namespace HIKARI::RENDER3D::REFLECTION
