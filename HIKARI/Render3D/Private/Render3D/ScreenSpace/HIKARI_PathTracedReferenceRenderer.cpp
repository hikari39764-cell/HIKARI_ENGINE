#include "Render3D/ScreenSpace/HIKARI_PathTracedReferenceRenderer.h"

#include <cstddef>
#include <cstring>

#include <d3dx12.h>

#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Gfx/HIKARI_VideoMemory.h"
#include "Render3D/Color/HIKARI_ViewExposure.h"
#include "Render3D/RayTracing/HIKARI_RuntimeRayQueryBindings.h"
#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingLighting.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"

namespace HIKARI::RENDER3D::SCREENSPACE {
namespace {

struct alignas(16) PathTracedReferenceCB {
    MATH::Mat4 invViewProj{};
    MATH::Vec4 screenParams{};
    uint32_t pathParams[4]{};
    MATH::Vec4 viewExposure{};
    RAYTRACING::RuntimeRayLightingGpuData lighting{};
};
static_assert(offsetof(PathTracedReferenceCB, lighting) == 112);

constexpr DXGI_FORMAT kAccumulationFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
constexpr DXGI_FORMAT kSceneColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr size_t kUploadBytes = 4096u;
constexpr uint32_t kMaxBounces = 8u;
constexpr const wchar_t* kShaderPath = L"HIKARI/Shaders/Post_PathTracedReference.hlsl";
// 共通の 0..10 の後：空(11)・累積の UAV(12)。合成は CBV(0)・累積の SRV(1)。
constexpr UINT kSkyParameter = RAYTRACING::kRuntimeRayQueryRootParameterCount;
constexpr UINT kAccumulationParameter = kSkyParameter + 1u;

// 累積を捨てる条件：ずらし無しの投影、場面（加速構造・材質）、太陽と空、局所光の表、出力の種類、大きさ。
uint64_t BuildAccumulationKey(const PathTracedReferenceInputs& inputs) {
    const auto& lighting = inputs.rayLighting->data;
    uint64_t key = HASH::Fnv1aBytes(HASH::kFnv1a64OffsetBasis, &inputs.temporal->camera.unjitteredViewProj,
                                    sizeof(inputs.temporal->camera.unjitteredViewProj));
    key = HASH::Fnv1aBytes(key, &lighting.sunDirectionIntensity, sizeof(MATH::Vec4) * 4u);
    key = HASH::Fnv1aBytes(key, &lighting.scene, sizeof(lighting.scene));
    const uint64_t values[]{inputs.rayLighting->scene.version,    inputs.rayLighting->lightTableHash,
                            inputs.materials->dataVersion,        inputs.materials->bindingVersion,
                            inputs.indirectDiffuseOnly ? 1u : 0u, (uint64_t(inputs.width) << 32u) | inputs.height};
    return HASH::Fnv1aBytes(key, values, sizeof(values));
}

} // namespace

bool PathTracedReferenceRenderer::Render(const GFX::Context& graphics, const PIPELINE::RenderTargetAccess& targetAccess,
                                         const PathTracedReferenceInputs& inputs) {
    auto* commands = graphics.cmdList;
    if (commands == nullptr || graphics.device == nullptr || graphics.srvHeap == nullptr ||
        inputs.temporal == nullptr || inputs.rayLighting == nullptr || inputs.materials == nullptr ||
        !*inputs.materials || inputs.width == 0u || inputs.height == 0u ||
        inputs.rayLighting->status != RAYTRACING::RuntimeRayLightingStatus::Ready || !inputs.rayLighting->scene.ready ||
        inputs.rayLighting->scene.accelerationStructure == 0 ||
        !RAYTRACING::CanTraceDiffuse(inputs.rayLighting->scene.excluded)) {
        return false;
    }
    if (!EnsurePipelines(graphics) || !EnsureAccumulation(graphics, inputs.width, inputs.height)) {
        return false;
    }
    auto& upload = uploads_[graphics.frameIndex % GFX::kFrameResourceCount];
    if (!upload.IsInitialized() &&
        !upload.Init(graphics.device, kUploadBytes, GFX::UPLOAD_TRAFFIC::Owner::PassConstants)) {
        return false;
    }
    upload.ResetForFrame(graphics.currentFrameRetireFenceValue);
    const auto slice = upload.Allocate(sizeof(PathTracedReferenceCB));
    if (!slice) {
        return false;
    }
    const uint64_t key = BuildAccumulationKey(inputs);
    if (key != accumulationKey_) {
        accumulationKey_ = key;
        sampleIndex_ = 0u;
    }
    PathTracedReferenceCB constants{};
    constants.invViewProj = inputs.temporal->camera.invUnjitteredViewProj;
    constants.screenParams = {static_cast<float>(width_), static_cast<float>(height_), 1.0f / float(width_),
                              1.0f / float(height_)};
    constants.pathParams[0] = sampleIndex_;
    constants.pathParams[1] = kMaxBounces;
    constants.pathParams[2] = inputs.indirectDiffuseOnly ? 1u : 0u;
    constants.viewExposure = COLOR::GetActiveViewExposure();
    constants.lighting = inputs.rayLighting->data;
    std::memcpy(slice.cpuAddress, &constants, sizeof(constants));

    GFX::PIX::ScopedGpuEvent event(commands, GFX::PIX::kColorPost, "PathTracedReference");
    GFX::GPU_PROFILE::ScopedGpuTimer timer(commands, GFX::GPU_PROFILE::Pass::PathTracedReference);
    ID3D12DescriptorHeap* heaps[]{graphics.srvHeap};
    commands->SetDescriptorHeaps(1, heaps);
    // 1) 路径を一本ずつ追跡して和へ足す（標本番号 0 は和を書き直す）。
    Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commands->SetComputeRootSignature(traceRootSignature_.Get());
    commands->SetComputeRootConstantBufferView(0, slice.gpuAddress);
    RAYTRACING::BindRuntimeRayQueryRoots(commands, graphics, *inputs.rayLighting, *inputs.materials, kSkyParameter);
    commands->SetComputeRootDescriptorTable(kAccumulationParameter, accumulationUav_.gpu);
    commands->SetPipelineState(tracePipeline_.Get());
    commands->Dispatch((width_ + 7u) / 8u, (height_ + 7u) / 8u, 1u);
    ++sampleIndex_;
    // 2) 面に当たった画素の平均を場面色へ。
    Transition(commands, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (!targetAccess.Rebind()) {
        return false;
    }
    commands->SetGraphicsRootSignature(compositeRootSignature_.Get());
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, float(width_), float(height_), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, LONG(width_), LONG(height_)};
    commands->RSSetViewports(1, &viewport);
    commands->RSSetScissorRects(1, &scissor);
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands->IASetVertexBuffers(0, 0, nullptr);
    commands->IASetIndexBuffer(nullptr);
    commands->SetGraphicsRootConstantBufferView(0, slice.gpuAddress);
    commands->SetGraphicsRootDescriptorTable(1, accumulationSrv_.gpu);
    commands->SetPipelineState(compositePipeline_.Get());
    commands->DrawInstanced(3, 1, 0, 0);
    return true;
}

bool PathTracedReferenceRenderer::EnsurePipelines(const GFX::Context& graphics) {
    if (traceRootSignature_ && compositeRootSignature_ && tracePipeline_ && compositePipeline_ &&
        deviceGeneration_ == graphics.deviceGeneration) {
        return true;
    }
    if (pipelineFailed_ && deviceGeneration_ == graphics.deviceGeneration) {
        return false;
    }
    traceRootSignature_.Reset();
    compositeRootSignature_.Reset();
    tracePipeline_.Reset();
    compositePipeline_.Reset();
    deviceGeneration_ = graphics.deviceGeneration;
    pipelineFailed_ = true;
    ID3D12Device* device = graphics.device;
    if (device == nullptr || !GFX::SupportsShaderModel6(device)) {
        return false;
    }

    std::array<CD3DX12_ROOT_PARAMETER, kAccumulationParameter + 1u> traceParameters;
    std::array<CD3DX12_DESCRIPTOR_RANGE, RAYTRACING::kRuntimeRayQueryRangeCount + 1u> traceRanges;
    RAYTRACING::InitRuntimeRayQueryRootParameters(traceParameters, traceRanges, kSkyParameter);
    traceRanges[RAYTRACING::kRuntimeRayQueryRangeCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 9);
    traceParameters[kAccumulationParameter].InitAsDescriptorTable(1,
                                                                  &traceRanges[RAYTRACING::kRuntimeRayQueryRangeCount]);
    const auto samplers = RAYTRACING::RuntimeRayQueryStaticSamplers();
    const CD3DX12_ROOT_SIGNATURE_DESC traceDesc(UINT(traceParameters.size()), traceParameters.data(),
                                                UINT(samplers.size()), samplers.data());
    if (!HIKARI_DX_CHECK(GFX::CreateRootSignature(device, traceDesc, traceRootSignature_.GetAddressOf()),
                         "PathTracedReference trace root signature")) {
        return false;
    }
    std::array<CD3DX12_ROOT_PARAMETER, 2> compositeParameters;
    CD3DX12_DESCRIPTOR_RANGE compositeRange;
    compositeRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 9);
    compositeParameters[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    compositeParameters[1].InitAsDescriptorTable(1, &compositeRange, D3D12_SHADER_VISIBILITY_PIXEL);
    const CD3DX12_ROOT_SIGNATURE_DESC compositeDesc(UINT(compositeParameters.size()), compositeParameters.data(), 0,
                                                    nullptr);
    if (!HIKARI_DX_CHECK(GFX::CreateRootSignature(device, compositeDesc, compositeRootSignature_.GetAddressOf()),
                         "PathTracedReference composite root signature")) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3DBlob> trace;
    Microsoft::WRL::ComPtr<ID3DBlob> vertex;
    Microsoft::WRL::ComPtr<ID3DBlob> pixel;
    if (!GFX::CompileShaderFileSm6(kShaderPath, "CSMain", L"cs_6_5", trace.GetAddressOf()) ||
        !GFX::CompileShaderFileSm6(kShaderPath, "VSMain", L"vs_6_5", vertex.GetAddressOf()) ||
        !GFX::CompileShaderFileSm6(kShaderPath, "PSComposite", L"ps_6_5", pixel.GetAddressOf())) {
        DEBUGLOG::PushRenderError("[PathTracedReference][ERROR] shader compile failed; the Lit image is shown.");
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC traceState{};
    traceState.pRootSignature = traceRootSignature_.Get();
    traceState.CS = {trace->GetBufferPointer(), trace->GetBufferSize()};
    if (!HIKARI_DX_CHECK(device->CreateComputePipelineState(&traceState, IID_PPV_ARGS(tracePipeline_.GetAddressOf())),
                         "PathTracedReference trace pipeline")) {
        return false;
    }
    // 場面色（HDR、深度付き）の RGB を置き換える。アルファと深度は触らない。
    D3D12_GRAPHICS_PIPELINE_STATE_DESC compositeState{};
    compositeState.pRootSignature = compositeRootSignature_.Get();
    compositeState.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
    compositeState.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
    compositeState.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    compositeState.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE;
    compositeState.SampleMask = UINT_MAX;
    compositeState.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    compositeState.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    compositeState.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    compositeState.DepthStencilState.DepthEnable = FALSE;
    compositeState.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    compositeState.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    compositeState.NumRenderTargets = 1;
    compositeState.RTVFormats[0] = kSceneColorFormat;
    compositeState.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    compositeState.SampleDesc.Count = 1;
    if (!HIKARI_DX_CHECK(
            device->CreateGraphicsPipelineState(&compositeState, IID_PPV_ARGS(compositePipeline_.GetAddressOf())),
            "PathTracedReference composite pipeline")) {
        return false;
    }
    pipelineFailed_ = false;
    return true;
}

bool PathTracedReferenceRenderer::EnsureAccumulation(const GFX::Context& graphics, uint32_t width, uint32_t height) {
    if (accumulation_ && width_ == width && height_ == height && accumulationSrv_.IsValid() &&
        accumulationUav_.IsValid()) {
        return true;
    }
    ReleaseAccumulation(graphics);
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(kAccumulationFormat, width, height, 1, 1, 1, 0,
                                                   D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    if (!HIKARI_DX_CHECK(GFX::VIDEO_MEMORY::CreateCommittedResource(
                             graphics.device, &heap, D3D12_HEAP_FLAG_NONE, &desc, state_, nullptr,
                             GFX::VIDEO_MEMORY::Owner::ScreenSpace, IID_PPV_ARGS(accumulation_.GetAddressOf())),
                         "PathTracedReference accumulation")) {
        return false;
    }
    accumulation_->SetName(L"HIKARI.PathTracedReference.Accumulation");
    accumulationSrv_ = AllocateTexture2DSrvDescriptor(accumulation_.Get(), kAccumulationFormat);
    accumulationUav_ = AllocateTexture2DUavDescriptor(accumulation_.Get(), kAccumulationFormat);
    if (!accumulationSrv_.IsValid() || !accumulationUav_.IsValid()) {
        return false;
    }
    width_ = width;
    height_ = height;
    sampleIndex_ = 0u;
    return true;
}

void PathTracedReferenceRenderer::ReleaseAccumulation(const GFX::Context& graphics) {
    if (accumulationSrv_.IsValid()) {
        RetireRenderResourceDescriptor(accumulationSrv_, graphics, "PathTracedReference.SRV");
    }
    if (accumulationUav_.IsValid()) {
        RetireRenderResourceDescriptor(accumulationUav_, graphics, "PathTracedReference.UAV");
    }
    if (accumulation_) {
        GFX::RetireD3D12ObjectForFrame(accumulation_, graphics, "PathTracedReference.Accumulation");
    }
    accumulationSrv_ = {};
    accumulationUav_ = {};
    accumulationKey_ = 0u;
    sampleIndex_ = 0u;
    width_ = 0u;
    height_ = 0u;
}

void PathTracedReferenceRenderer::Transition(ID3D12GraphicsCommandList* commands, D3D12_RESOURCE_STATES next) {
    if (state_ == next || !accumulation_) {
        state_ = next;
        return;
    }
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(accumulation_.Get(), state_, next);
    commands->ResourceBarrier(1, &barrier);
    state_ = next;
}

void PathTracedReferenceRenderer::Release() {
    ReleaseAccumulation(GFX::GetCurrentContext());
    for (auto& upload : uploads_) {
        upload.Finalize();
    }
    traceRootSignature_.Reset();
    compositeRootSignature_.Reset();
    tracePipeline_.Reset();
    compositePipeline_.Reset();
    deviceGeneration_ = 0u;
    pipelineFailed_ = false;
}

} // namespace HIKARI::RENDER3D::SCREENSPACE
